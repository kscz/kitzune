#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"

// ESP-ADF stuff
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"

// Pull in ALL THE DECODERS!
#include "mp3_decoder.h"
#include "opus_decoder.h"
#include "ogg_decoder.h"
#include "flac_decoder.h"
#include "wav_decoder.h"
#include "aac_decoder.h"

#include "filter_resample.h"

#include "esp_peripherals.h"
#include "periph_service.h"
#include "periph_sdcard.h"
#include "playlist.h"
#include "sdcard_list.h"
#include "board.h"

#include "a2dp_stream.h"
#include "esp_avrc_api.h"

#include "bt_be.h"
#include "kz_util.h"
#include "lvgl.h"
#include "ui_common.h"
#include "ui_np.h"

#define PLAYER_DECODE_IN_PSRAM (true)
#define PLAYER_DECODER_OUT_BUF_SIZE (16 * 1024)
#define ELEMENT_INPUT_TIMEOUT_MS 500

typedef enum {
    PLAYER_BE_PLAYLIST_MSG,
    PLAYER_BE_PLAYPAUSE_MSG,
    PLAYER_BE_NEXT_MSG,
    PLAYER_BE_BT_HEADPHONES_MSG,
    PLAYER_BE_BT_DISCONNECT_MSG,
    PLAYER_BE_BT_DISABLE_MSG,
} player_be_msg_type;

typedef struct {
    player_be_msg_type type; // always PLAYER_BE_PLAYLIST_MSG
    playlist_operator_t *pl_op;
} playlist_msg;

typedef union {
    player_be_msg_type type;
    playlist_msg pl_msg;
} player_be_msg_u;

static const char *TAG = "PLAYER_BE";

// queue used to manage passing messages from other threads to the player
static QueueHandle_t s_player_be_queue = NULL;

static playlist_operator_handle_t s_playlist = NULL;
static playlist_operation_t s_pl_oper; // only valid if s_playlist is non-NULL 
static uint32_t s_playlist_len = 0;

static audio_pipeline_handle_t s_pipeline = NULL;
static audio_element_handle_t s_hp_stream, s_fs_stream, s_bt_hp_stream;
static audio_element_handle_t s_mp3_stream, s_flac_stream, s_aac_stream,
                              s_wav_stream, s_ogg_stream, s_opus_stream;
static audio_element_handle_t s_resampler = NULL;
static audio_element_handle_t s_current_decoder = NULL;
static const char *s_current_ext_str = NULL;
static audio_extension_e s_current_ext = AUD_EXT_UNKNOWN;
static audio_event_iface_handle_t s_evt;

static TaskHandle_t s_task = NULL;

// Position from the decoder's byte_pos: exact for CBR, drifts on VBR
static int64_t s_data_start = 0;
static int s_bps = 0;
static int s_total_sec = 0;
static int s_elapsed_sec = -1;

static bool s_playmode_is_shuffle = true;
static bool s_hp_is_bt = false, s_hp_is_bt_desired = false;
static esp_periph_handle_t s_bt_periph;

// A2DP renders at the sink, so absolute volume is the only volume we have.
// Written from the BTC task as well as from whoever presses a key.
#define BT_VOLUME_STEP 5
static int s_bt_volume = 50;
static bool s_bt_vol_registered = false;
static bool s_bt_avrc_tg_connected = false;

void player_be_init(esp_periph_handle_t bt_periph) {
    s_bt_periph = bt_periph;
}

static uint8_t bt_volume_to_avrc(int volume) {
    return (uint8_t)((volume * 127) / 100);
}

static void bt_set_volume(int volume) {
    if (volume > 100) {
        volume = 100;
    } else if (volume < 0) {
        volume = 0;
    }
    s_bt_volume = volume;

    if (!s_bt_vol_registered) {
        ESP_LOGW(TAG, "[ * ] Sink hasn't registered for volume changes");
        return;
    }

    // The response consumes the registration; the sink has to ask again
    s_bt_vol_registered = false;

    esp_avrc_rn_param_t rn_param = { .volume = bt_volume_to_avrc(volume) };
    esp_err_t ret = esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE,
                                            ESP_AVRC_RN_RSP_CHANGED, &rn_param);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[ * ] Unable to notify the sink: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "[ * ] BT volume set to %d %%", volume);
}

// a2dp_stream_init() installs its own handler, which reports the pre-change
// volume and answers whether or not the sink registered. We replace it.
static void player_avrc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
    switch (event) {
        case ESP_AVRC_TG_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "AVRC TG connection state: %d", param->conn_stat.connected);
            s_bt_avrc_tg_connected = param->conn_stat.connected;
            if (!param->conn_stat.connected) {
                s_bt_vol_registered = false;
            }
            break;
        case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
            s_bt_volume = (param->set_abs_vol.volume * 100) / 127;
            ESP_LOGI(TAG, "AVRC volume set by the sink to %d %%", s_bt_volume);
            break;
        case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
            ESP_LOGI(TAG, "AVRC register notification: event %d", param->reg_ntf.event_id);
            if (param->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
                s_bt_vol_registered = true;
                esp_avrc_rn_param_t rn_param = { .volume = bt_volume_to_avrc(s_bt_volume) };
                esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE,
                                        ESP_AVRC_RN_RSP_INTERIM, &rn_param);
            }
            break;
        case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
            ESP_LOGI(TAG, "AVRC TG remote features %x, CT features %x",
                     (unsigned int)param->rmt_feats.feat_mask,
                     (unsigned int)param->rmt_feats.ct_feat_flag);
            break;
        default:
            break;
    }
}

void player_be_volume_up(audio_board_handle_t board_handle) {
    if (!s_hp_is_bt) {
        int player_volume;
        audio_hal_get_volume(board_handle->audio_hal, &player_volume);
        player_volume += 2;
        if (player_volume > 100) {
            player_volume = 100;
        }
        audio_hal_set_volume(board_handle->audio_hal, player_volume);
        ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
    } else if (s_bt_avrc_tg_connected) {
        bt_set_volume(s_bt_volume + BT_VOLUME_STEP);
    } else {
        // Sinks that drive volume themselves take the passthrough instead
        periph_bt_volume_up(s_bt_periph);
    }
}

void player_be_volume_down(audio_board_handle_t board_handle) {
    if (!s_hp_is_bt) {
        int player_volume;
        audio_hal_get_volume(board_handle->audio_hal, &player_volume);
        player_volume -= 2;
        if (player_volume < 0) {
            player_volume = 0;
        }
        audio_hal_set_volume(board_handle->audio_hal, player_volume);
        ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
    } else if (s_bt_avrc_tg_connected) {
        bt_set_volume(s_bt_volume - BT_VOLUME_STEP);
    } else {
        periph_bt_volume_down(s_bt_periph);
    }
}

BaseType_t player_set_playlist(playlist_operator_handle_t new_playlist, TickType_t ticksToWait) {
    player_be_msg_u m;
    m.pl_msg.type = PLAYER_BE_PLAYLIST_MSG;
    m.pl_msg.pl_op = new_playlist;
    xQueueSendToBack(s_player_be_queue, &m, ticksToWait);
    xTaskAbortDelay(s_task);
    return 0;
}

esp_err_t player_playpause(void) {
    player_be_msg_u msg;
    msg.type = PLAYER_BE_PLAYPAUSE_MSG;
    xQueueSendToBack(s_player_be_queue, &msg, 0);
    xTaskAbortDelay(s_task);
    return ESP_OK;
}

// False until a2dp_stream_init() has run: esp_a2d_source_connect() asserts
bool player_be_bt_ready(void) {
    return (s_bt_hp_stream != NULL);
}

esp_err_t player_be_set_bt_hp(void) {
    if (s_bt_hp_stream == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    player_be_msg_u msg;
    msg.type = PLAYER_BE_BT_HEADPHONES_MSG;
    xQueueSendToBack(s_player_be_queue, &msg, 0);
    xTaskAbortDelay(s_task);
    return ESP_OK;
}

esp_err_t player_be_disable_bt(void) {
    if (s_bt_hp_stream == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    player_be_msg_u msg;
    msg.type = PLAYER_BE_BT_DISABLE_MSG;
    if (pdPASS != xQueueSendToBack(s_player_be_queue, &msg, 0)) {
        return ESP_FAIL;
    }
    xTaskAbortDelay(s_task);
    return ESP_OK;
}

// Elements block on their input ringbuffer forever by default. audio_pipeline_pause()
// pauses upstream first, so whichever element it starves next never returns from
// that read to handle its own PAUSE, and a later resume skips it for still being
// RUNNING. A finite read timeout gets it back to its message queue.
static void set_input_timeouts(void) {
    audio_element_handle_t els[] = {
        s_mp3_stream, s_flac_stream, s_opus_stream, s_ogg_stream,
        s_wav_stream, s_aac_stream, s_resampler, s_hp_stream,
    };

    for (int i = 0; i < (int)(sizeof(els) / sizeof(els[0])); ++i) {
        if (els[i] != NULL) {
            audio_element_set_input_timeout(els[i], pdMS_TO_TICKS(ELEMENT_INPUT_TIMEOUT_MS));
        }
    }
}

static esp_err_t playpause_playlist(void) {
    audio_element_state_t el_state = s_hp_is_bt ?
       audio_element_get_state(s_bt_hp_stream) : audio_element_get_state(s_hp_stream);
    switch (el_state) {
        case AEL_STATE_INIT :
            ESP_LOGI(TAG, "Starting audio pipeline");
            if (s_hp_is_bt) {
                periph_bt_play(s_bt_periph);
            }
            audio_pipeline_run(s_pipeline);
            ui_set_play(true);
            break;
        case AEL_STATE_RUNNING :
            ESP_LOGI(TAG, "Pausing audio pipeline");
            audio_pipeline_pause(s_pipeline);
            if (s_hp_is_bt) {
                periph_bt_pause(s_bt_periph);
            }
            ui_set_play(false);
            break;
        case AEL_STATE_PAUSED :
            ESP_LOGI(TAG, "Resuming audio pipeline");
            if (s_hp_is_bt) {
                periph_bt_play(s_bt_periph);
            }
            audio_pipeline_resume(s_pipeline);
            ui_set_play(true);
            break;
        default :
            ESP_LOGI(TAG, "Unsupported state %d", el_state);
            return ESP_FAIL;
    }

    return ESP_OK;
}

static void set_decoder_info(audio_extension_e ext) {
    switch (ext) {
        case AUD_EXT_MP3:
            s_current_decoder = s_mp3_stream;
            s_current_ext = ext;
            s_current_ext_str = "mp3";
            break;
        case AUD_EXT_FLAC:
            s_current_decoder = s_flac_stream;
            s_current_ext = ext;
            s_current_ext_str = "flac";
            break;
        case AUD_EXT_OPUS:
            s_current_decoder = s_opus_stream;
            s_current_ext = ext;
            s_current_ext_str = "opus";
            break;
        case AUD_EXT_OGG:
            s_current_decoder = s_ogg_stream;
            s_current_ext = ext;
            s_current_ext_str = "ogg";
            break;
        case AUD_EXT_WAV:
            s_current_decoder = s_wav_stream;
            s_current_ext = ext;
            s_current_ext_str = "wav";
            break;
        case AUD_EXT_MP4:
        case AUD_EXT_AAC:
        case AUD_EXT_M4A:
        case AUD_EXT_TS:
            s_current_decoder = s_aac_stream;
            s_current_ext = ext;
            s_current_ext_str = "aac";
            break;
        default:
            s_current_decoder = NULL;
            s_current_ext = AUD_EXT_UNKNOWN;
            s_current_ext_str = NULL;
    }
}

esp_err_t player_next(void) {
    player_be_msg_u msg;
    msg.type = PLAYER_BE_NEXT_MSG;
    xQueueSendToBack(s_player_be_queue, &msg, 0);
    xTaskAbortDelay(s_task);
    return ESP_OK;
}

void player_set_shuffle(bool is_shuffle) {
    s_playmode_is_shuffle = is_shuffle;
}

bool player_get_shuffle(void) {
    return s_playmode_is_shuffle;
}

// The seek callback maps a timestamp to a file offset, so t=0 is where the
// audio data starts. Decoders without one leave us at 0.
static int64_t decoder_data_start(int64_t total_bytes) {
    int seek_sec = 0, byte_pos = 0, out_size = 0;

    if (s_current_decoder == NULL) {
        return 0;
    }
    if (ESP_OK != audio_element_seek(s_current_decoder, &seek_sec, sizeof(seek_sec),
                                     &byte_pos, &out_size)) {
        return 0;
    }
    if (out_size != sizeof(byte_pos) || byte_pos < 0 || byte_pos >= total_bytes) {
        return 0;
    }
    return byte_pos;
}

// flac reports total_bytes only on close, so fall back to the file size
static int64_t track_total_bytes(const audio_element_info_t *dec_info) {
    if (dec_info->total_bytes > 0) {
        return dec_info->total_bytes;
    }

    audio_element_info_t fs_info = {0};
    audio_element_getinfo(s_fs_stream, &fs_info);
    return fs_info.total_bytes;
}

static void reset_play_time(void) {
    s_data_start = 0;
    s_bps = 0;
    s_total_sec = 0;
    s_elapsed_sec = -1;
    ui_np_set_time(0, 0);
}

static void update_play_time(void) {
    if (s_current_decoder == NULL) {
        return;
    }

    audio_element_info_t info = {0};
    audio_element_getinfo(s_current_decoder, &info);

    int64_t total_bytes = track_total_bytes(&info);
    if (total_bytes <= 0) {
        return;
    }

    int elapsed;
    if (info.bps > 0) {
        if (s_bps <= 0) {
            s_bps = info.bps;
            // flac's seek callback divides by a duration it hasn't published
            // yet, so never ask that one
            s_data_start = (s_current_ext != AUD_EXT_FLAC) ?
                    decoder_data_start(total_bytes) : 0;
            s_total_sec = (total_bytes > s_data_start) ?
                    (int)(((total_bytes - s_data_start) * 8) / s_bps) : 0;
        }

        int64_t played = info.byte_pos - s_data_start;
        if (played < 0) {
            played = 0;
        }
        elapsed = (int)((played * 8) / s_bps);
    } else if (info.duration > 0) {
        // flac only ever publishes a duration, in ms
        s_total_sec = info.duration / 1000;
        elapsed = (int)((info.byte_pos * s_total_sec) / total_bytes);
    } else {
        return;
    }

    if (s_total_sec > 0 && elapsed > s_total_sec) {
        elapsed = s_total_sec;
    }
    if (elapsed == s_elapsed_sec) {
        return;
    }

    s_elapsed_sec = elapsed;
    ui_np_set_time(elapsed, s_total_sec);
}

static void configure_and_run_playlist(const char *url) {
    audio_pipeline_stop(s_pipeline);
    if (s_hp_is_bt) {
        periph_bt_stop(s_bt_periph);
    }
    audio_pipeline_wait_for_stop(s_pipeline);

    audio_extension_e ext = AUD_EXT_UNKNOWN;
    if (url != NULL) {
        ESP_LOGI(TAG, "URL: %s", url);
        ui_np_set_song_title(url + 14);
        ext = kz_get_ext(url);
        audio_element_set_uri(s_fs_stream, url);
    }
    reset_play_time();
    audio_pipeline_reset_ringbuffer(s_pipeline);
    audio_pipeline_reset_elements(s_pipeline);
    audio_pipeline_change_state(s_pipeline, AEL_STATE_INIT);

    if ((ext != AUD_EXT_UNKNOWN && s_current_ext != ext) ||
            s_hp_is_bt_desired != s_hp_is_bt)
    {
        s_hp_is_bt = s_hp_is_bt_desired;
        audio_pipeline_unlink(s_pipeline);
        audio_element_terminate(s_current_decoder);

        if (ext != AUD_EXT_UNKNOWN) {
            set_decoder_info(ext);
        }

        if (s_hp_is_bt) {
            audio_pipeline_relink(s_pipeline, (const char *[]) {"fs", s_current_ext_str, "rsp", "bt"}, 4);
        } else {
            audio_pipeline_relink(s_pipeline, (const char *[]) {"fs", s_current_ext_str, "hp"}, 3);
        }
        audio_pipeline_set_listener(s_pipeline, s_evt);
    }
    if (s_hp_is_bt) {
        periph_bt_play(s_bt_periph);
    }
    audio_pipeline_run(s_pipeline);
    ui_set_play(true);
}

// Runs on the player task: the pipeline and the a2dp element are ours, so a
// teardown from any other task races with playback.
static void disable_bt(void) {
    if (s_bt_hp_stream == NULL) {
        return;
    }

    // Move the output back to the codec so nothing feeds "bt" any more
    s_hp_is_bt_desired = false;
    if (s_hp_is_bt) {
        configure_and_run_playlist(NULL);
    }

    audio_pipeline_unregister(s_pipeline, s_bt_hp_stream);
    audio_element_deinit(s_bt_hp_stream);
    s_bt_hp_stream = NULL;

    // Stays in the periph set, but stop it and drop our handle so no later
    // transport/volume call reaches the dead stack
    if (s_bt_periph != NULL) {
        esp_periph_stop(s_bt_periph);
        s_bt_periph = NULL;
    }

    s_bt_avrc_tg_connected = false;
    s_bt_vol_registered = false;

    // a2dp_stream_init() brought up a2dp plus both avrc roles
    a2dp_destroy();
    esp_avrc_tg_deinit();
    esp_avrc_ct_deinit();

    bt_be_deinit();
}

static void advance_playlist() {
    char *url = NULL;
    if (s_playmode_is_shuffle) {
        uint32_t next_song = esp_random() % s_playlist_len;
        s_pl_oper.choose(s_playlist, next_song, &url);
    } else {
        s_pl_oper.next(s_playlist, 1, &url);
    }
    configure_and_run_playlist(url);
}

static void player_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    switch(event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "CONNECTION_STATE_EVT: state %d", param->conn_stat.state);
            // Covers a failed connect too: the pipeline is switched over as
            // soon as the device is picked, before the link is up
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                player_be_msg_u msg;
                msg.type = PLAYER_BE_BT_DISCONNECT_MSG;
                xQueueSendToBack(s_player_be_queue, &msg, 0);
                xTaskAbortDelay(s_task);
            }
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            ESP_LOGI(TAG, "AUDIO_STATE_EVT");
            break;
        case ESP_A2D_AUDIO_CFG_EVT:
            ESP_LOGI(TAG, "AUDIO_CFG_EVT");
            break;
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            ESP_LOGI(TAG, "MEDIA_CTRL_ACK_EVT");
            if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY && param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                // Nothing feeds "bt" until it's linked, and starting anyway
                // just underflows the sink. periph_bt_play() re-checks later.
                if (s_bt_hp_stream == NULL || audio_element_get_input_ringbuf(s_bt_hp_stream) == NULL) {
                    ESP_LOGI(TAG, "a2dp media ready, but nothing to send yet");
                    break;
                }
                ESP_LOGI(TAG, "a2dp media ready, starting ...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            } else if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START && param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                ESP_LOGI(TAG, "a2dp media start successfully.");
            } else if (param->media_ctrl_stat.cmd != ESP_A2D_MEDIA_CTRL_SUSPEND) {
                // not started successfully, transfer to idle state
                ESP_LOGI(TAG, "a2dp media start failed.");
            }
            break;
        case ESP_A2D_PROF_STATE_EVT:
            ESP_LOGI(TAG, "PROF_STATE_EVT");
            break;
        case ESP_A2D_SNK_PSC_CFG_EVT:
            ESP_LOGI(TAG, "SNK_PSC_CFG_EVT");
            break;
        case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
            ESP_LOGI(TAG, "SNK_SET_DELAY_VALUE_EVT");
            break;
        case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
            ESP_LOGI(TAG, "SNK_GET_DELAY_VALUE_EVT");
            break;
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
            ESP_LOGI(TAG, "REPORT_SNK_DELAY_VALUE_EVT");
            break;
        case ESP_A2D_SEP_REG_STATE_EVT:
            ESP_LOGI(TAG, "SEP_REG_STATE_EVT");
            break;
    }
}

void player_main(void *arg) {
    s_player_be_queue = xQueueCreate(4, sizeof(player_be_msg_u));
    s_task = xTaskGetHandle("PLAYER");

    // create an empty pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(s_pipeline);

    // Initialize the I2S stream
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    s_hp_stream = i2s_stream_init(&i2s_cfg);

    // Initialize the FATFS file reader stream
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    s_fs_stream = fatfs_stream_init(&fatfs_cfg);

    player_be_msg_u be_msg;
    char *url = NULL;

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.out_rb_size = PLAYER_DECODER_OUT_BUF_SIZE;
    mp3_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_mp3_stream = mp3_decoder_init(&mp3_cfg);

    flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
    flac_cfg.out_rb_size = PLAYER_DECODER_OUT_BUF_SIZE;
    flac_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_flac_stream  = flac_decoder_init(&flac_cfg);

    opus_decoder_cfg_t opus_cfg = DEFAULT_OPUS_DECODER_CONFIG();
    opus_cfg.out_rb_size = PLAYER_DECODER_OUT_BUF_SIZE;
    opus_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_opus_stream  = decoder_opus_init(&opus_cfg);

    ogg_decoder_cfg_t ogg_cfg = DEFAULT_OGG_DECODER_CONFIG();
    ogg_cfg.out_rb_size = PLAYER_DECODER_OUT_BUF_SIZE;
    ogg_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_ogg_stream  = ogg_decoder_init(&ogg_cfg);

    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    wav_cfg.out_rb_size = PLAYER_DECODER_OUT_BUF_SIZE;
    wav_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_wav_stream  = wav_decoder_init(&wav_cfg);

    aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
    aac_cfg.out_rb_size = PLAYER_DECODER_OUT_BUF_SIZE;
    aac_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_aac_stream  = aac_decoder_init(&aac_cfg);

    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 48000;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = 44100;
    rsp_cfg.dest_ch = 2;
    rsp_cfg.mode = RESAMPLE_DECODE_MODE;
    rsp_cfg.complexity = 0;
    rsp_cfg.out_rb_size = PLAYER_DECODER_OUT_BUF_SIZE;
    s_resampler = rsp_filter_init(&rsp_cfg);

    a2dp_stream_config_t a2dp_config = {
        .type = AUDIO_STREAM_WRITER,
        .user_callback = {
            .user_a2d_cb = player_a2d_cb
        },
    };
    s_bt_hp_stream = a2dp_stream_init(&a2dp_config);

    // Must follow a2dp_stream_init(): it registers a handler of its own
    esp_avrc_tg_register_callback(player_avrc_tg_cb);

    // at this point we should have everything we need to start playing!
    // build up the pipeline!
    audio_pipeline_register(s_pipeline, s_fs_stream, "fs");
    audio_pipeline_register(s_pipeline, s_mp3_stream, "mp3");
    audio_pipeline_register(s_pipeline, s_flac_stream, "flac");
    audio_pipeline_register(s_pipeline, s_opus_stream, "opus");
    audio_pipeline_register(s_pipeline, s_ogg_stream, "ogg");
    audio_pipeline_register(s_pipeline, s_wav_stream, "wav");
    audio_pipeline_register(s_pipeline, s_aac_stream, "aac");
    audio_pipeline_register(s_pipeline, s_hp_stream, "hp");
    audio_pipeline_register(s_pipeline, s_resampler, "rsp");
    audio_pipeline_register(s_pipeline, s_bt_hp_stream, "bt");

    set_input_timeouts();

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    s_evt = audio_event_iface_init(&evt_cfg);

    // Only the link needs a song: it's named after the decoder
    while (s_playlist == NULL) {
        xQueueReceive(s_player_be_queue, &be_msg, portMAX_DELAY);
        if (be_msg.type == PLAYER_BE_PLAYLIST_MSG) {
            s_playlist = be_msg.pl_msg.pl_op;
        } else if (be_msg.type == PLAYER_BE_BT_HEADPHONES_MSG) {
            s_hp_is_bt_desired = true;
        } else if (be_msg.type == PLAYER_BE_BT_DISCONNECT_MSG) {
            s_hp_is_bt_desired = false;
        } else if (be_msg.type == PLAYER_BE_BT_DISABLE_MSG) {
            disable_bt();
        }
    }

    // Now that we have a valid playlist, setup our associated data
    s_playlist->get_operation(&s_pl_oper);
    s_playlist_len = (uint32_t)s_pl_oper.get_url_num(s_playlist);

    // set the fatfs stream to point at the start
    if (s_playmode_is_shuffle) {
        uint32_t next_song = esp_random() % s_playlist_len;
        s_pl_oper.choose(s_playlist, next_song, &url);
    } else {
        s_pl_oper.current(s_playlist, &url);
    }
    ui_np_set_song_title(url + 14);
    audio_element_set_uri(s_fs_stream, url);
    set_decoder_info(kz_get_ext(url));

    s_hp_is_bt = s_hp_is_bt_desired;
    if (s_hp_is_bt) {
        audio_pipeline_link(s_pipeline, (const char *[]) {"fs", s_current_ext_str, "rsp", "bt"}, 4);
    } else {
        audio_pipeline_link(s_pipeline, (const char *[]) {"fs", s_current_ext_str, "hp"}, 3);
    }

    audio_pipeline_set_listener(s_pipeline, s_evt);

    while (1) {
        audio_event_iface_msg_t msg;
        while (pdPASS == xQueueReceive(s_player_be_queue, &be_msg, 0)) {
            if (be_msg.type == PLAYER_BE_NEXT_MSG) {
                advance_playlist();
            } else if (be_msg.type == PLAYER_BE_PLAYPAUSE_MSG) {
                playpause_playlist();
            } else if (be_msg.type == PLAYER_BE_PLAYLIST_MSG) {
                ESP_LOGI(TAG, "Received a playlist!");
                s_pl_oper.destroy(s_playlist);
                s_playlist = be_msg.pl_msg.pl_op;
                // setup our associated data
                s_playlist->get_operation(&s_pl_oper);
                s_playlist_len = (uint32_t)s_pl_oper.get_url_num(s_playlist);
                if (s_playmode_is_shuffle) {
                    uint32_t next_song = esp_random() % s_playlist_len;
                    s_pl_oper.choose(s_playlist, next_song, &url);
                } else {
                    s_pl_oper.current(s_playlist, &url);
                }
                configure_and_run_playlist(url);
            } else if (be_msg.type == PLAYER_BE_BT_HEADPHONES_MSG) {
                if (s_bt_hp_stream == NULL) {
                    ESP_LOGW(TAG, "Bluetooth is disabled, ignoring");
                } else {
                    s_hp_is_bt_desired = true;
                    configure_and_run_playlist(NULL);
                }
            } else if (be_msg.type == PLAYER_BE_BT_DISCONNECT_MSG) {
                if (s_hp_is_bt) {
                    ESP_LOGI(TAG, "Bluetooth dropped, back to the jack");
                    s_hp_is_bt_desired = false;
                    configure_and_run_playlist(NULL);
                }
            } else if (be_msg.type == PLAYER_BE_BT_DISABLE_MSG) {
                disable_bt();
            }
        }
        update_play_time();
        if (ESP_OK != audio_event_iface_listen(s_evt, &msg, pdMS_TO_TICKS(1000))) {
            continue;
        }
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
            // Set music info for a new song to be played
            if (msg.source == (void *) s_current_decoder
                && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(s_current_decoder, &music_info);
                ESP_LOGI(TAG, "[ * ] Received music info from decoder, sample_rates=%d, bits=%d, ch=%d, dur=%d",
                         music_info.sample_rates, music_info.bits, music_info.channels, music_info.duration);
                i2s_stream_set_clk(s_hp_stream, music_info.sample_rates, music_info.bits, music_info.channels);
                rsp_filter_change_src_info(s_resampler, music_info.sample_rates, music_info.channels, music_info.bits);
                if (!s_hp_is_bt) {
                    audio_element_setinfo(s_hp_stream, &music_info);
                } else {
                    audio_element_setinfo(s_bt_hp_stream, &music_info);
                }
                update_play_time();
                continue;
            }
            if (msg.source == (void *) s_current_decoder
                && msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                audio_element_state_t el_state = audio_element_get_state(s_current_decoder);
                if (el_state == AEL_STATE_FINISHED) {
                    ESP_LOGI(TAG, "[ * ] Finished, advancing to the next song");
                    advance_playlist();
                }
            }
        }
    }
}
