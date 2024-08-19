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

#include "esp_peripherals.h"
#include "periph_service.h"
#include "periph_sdcard.h"
#include "playlist.h"
#include "sdcard_list.h"
#include "board.h"

#include "kz_util.h"
#include "lvgl.h"
#include "ui_common.h"
#include "ui_np.h"

#define PLAYER_DECODE_IN_PSRAM (true)

typedef enum {
    PLAYER_BE_PLAYLIST_MSG,
    PLAYER_BE_PLAYPAUSE_MSG,
    PLAYER_BE_NEXT_MSG,
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
static audio_element_handle_t s_hp_stream, s_fs_stream;
static audio_element_handle_t s_mp3_stream, s_flac_stream, s_aac_stream,
                              s_wav_stream, s_ogg_stream, s_opus_stream;
static audio_element_handle_t s_current_decoder = NULL;
static const char *s_current_ext_str = NULL;
static audio_extension_e s_current_ext = AUD_EXT_UNKNOWN;
static bool s_playmode_is_shuffle = true;
static audio_event_iface_handle_t s_evt;

static TaskHandle_t s_task = NULL;

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

static esp_err_t playpause_playlist(void) {
    audio_element_state_t el_state = audio_element_get_state(s_hp_stream);
    switch (el_state) {
        case AEL_STATE_INIT :
            ESP_LOGI(TAG, "Starting audio pipeline");
            audio_pipeline_run(s_pipeline);
            break;
        case AEL_STATE_RUNNING :
            ESP_LOGI(TAG, "Pausing audio pipeline");
            audio_pipeline_pause(s_pipeline);
            break;
        case AEL_STATE_PAUSED :
            ESP_LOGI(TAG, "Resuming audio pipeline");
            audio_pipeline_resume(s_pipeline);
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

static void configure_and_run_playlist(const char *url) {
    ESP_LOGI(TAG, "URL: %s", url);
    ui_np_set_song_title(url);
    audio_pipeline_stop(s_pipeline);
    audio_pipeline_wait_for_stop(s_pipeline);

    audio_extension_e ext = kz_get_ext(url);
    audio_element_set_uri(s_fs_stream, url);
    audio_pipeline_reset_ringbuffer(s_pipeline);
    audio_pipeline_reset_elements(s_pipeline);
    audio_pipeline_change_state(s_pipeline, AEL_STATE_INIT);

    if (s_current_ext != ext) {
        audio_pipeline_unlink(s_pipeline);
        set_decoder_info(ext);
        audio_pipeline_relink(s_pipeline, (const char *[]) {"fs", s_current_ext_str, "hp"}, 3);
        audio_pipeline_set_listener(s_pipeline, s_evt);
    }
    audio_pipeline_run(s_pipeline);
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

void player_main(void) {
    s_player_be_queue = xQueueCreate(4, sizeof(player_be_msg_u));
    s_task = xTaskGetHandle("PLAYER");

    // create an empty pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(s_pipeline);

    // Initialize the I2S stream
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_config.sample_rate = 48000; // Just set it to an easy default
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    s_hp_stream = i2s_stream_init(&i2s_cfg);

    // Initialize the FATFS file reader stream
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    s_fs_stream = fatfs_stream_init(&fatfs_cfg);

    // Loop until we get a valid playlist
    player_be_msg_u be_msg;
    while (s_playlist == NULL) {
        xQueueReceive(s_player_be_queue, &be_msg, portMAX_DELAY);
        if (be_msg.type == PLAYER_BE_PLAYLIST_MSG) {
            s_playlist = be_msg.pl_msg.pl_op;
        }
    }
    // Now that we have a valid playlist, setup our associated data
    s_playlist->get_operation(&s_pl_oper);
    s_playlist_len = (uint32_t)s_pl_oper.get_url_num(s_playlist);

    // set the fatfs stream to point at the start
    char *url = NULL;
    if (s_playmode_is_shuffle) {
        uint32_t next_song = esp_random() % s_playlist_len;
        s_pl_oper.choose(s_playlist, next_song, &url);
    } else {
        s_pl_oper.current(s_playlist, &url);
    }
    ui_np_set_song_title(url);
    audio_element_set_uri(s_fs_stream, url);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.out_rb_size = (16 * 1024);
    mp3_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_mp3_stream = mp3_decoder_init(&mp3_cfg);

    flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
    flac_cfg.out_rb_size = (16 * 1024);
    flac_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_flac_stream  = flac_decoder_init(&flac_cfg);

    opus_decoder_cfg_t opus_cfg = DEFAULT_OPUS_DECODER_CONFIG();
    opus_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_opus_stream  = decoder_opus_init(&opus_cfg);

    ogg_decoder_cfg_t ogg_cfg = DEFAULT_OGG_DECODER_CONFIG();
    ogg_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_ogg_stream  = ogg_decoder_init(&ogg_cfg);

    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    wav_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_wav_stream  = wav_decoder_init(&wav_cfg);

    aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
    aac_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
    s_aac_stream  = aac_decoder_init(&aac_cfg);

    set_decoder_info(kz_get_ext(url));

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

    const char *link_tag[3] = {"fs", s_current_ext_str, "hp"};
    audio_pipeline_link(s_pipeline, &link_tag[0], 3);

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    s_evt = audio_event_iface_init(&evt_cfg);

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
            }
        }
        if (ESP_OK != audio_event_iface_listen(s_evt, &msg, portMAX_DELAY)) {
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
                audio_element_setinfo(s_hp_stream, &music_info);
                continue;
            }
            // Advance to the next song when previous finishes
            if (msg.source == (void *) s_hp_stream
                && msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                audio_element_state_t el_state = audio_element_get_state(s_hp_stream);
                if (el_state == AEL_STATE_FINISHED) {
                    ESP_LOGI(TAG, "[ * ] Finished, advancing to the next song");
                    advance_playlist();
                }
            }
        }
    }

//    ESP_LOGI(TAG, "[ 7 ] Stop audio_pipeline");
//    audio_pipeline_stop(s_pipeline);
//    audio_pipeline_wait_for_stop(s_pipeline);
//    audio_pipeline_terminate(s_pipeline);
//
//    audio_pipeline_unregister(s_pipeline, s__decoder);
//    audio_pipeline_unregister(s_pipeline, i2s_stream_writer);
//
//    /* Terminate the pipeline before removing the listener */
//    audio_pipeline_remove_listener(pipeline);
//
//    /* Stop all peripherals before removing the listener */
//    esp_periph_set_stop_all(set);
//    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);
//
//    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
//    audio_event_iface_destroy(evt);
//
//    /* Release all resources */
//    audio_pipeline_deinit(pipeline);
//    audio_element_deinit(i2s_stream_writer);
//    audio_element_deinit(flac_decoder);
}
