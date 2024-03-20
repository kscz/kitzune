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
#include "periph_sdcard.h"
#include "playlist.h"
#include "sdcard_list.h"
#include "board.h"

#define PLAYER_DECODE_IN_PSRAM (true)

static const char *TAG = "PLAYER_BE";

// internal queue used to manage new playlists
static QueueHandle_t s_player_be_queue = NULL;

static playlist_operator_handle_t s_playlist = NULL;
static playlist_operation_t s_pl_oper; // only valid if s_playlist is non-NULL 
static uint32_t s_playlist_len = 0;

static audio_pipeline_handle_t s_pipeline = NULL;
static audio_element_handle_t s_hp_stream, s_decode_stream, s_fs_stream;
static bool s_playmode_is_shuffle = true;

typedef enum {
    AUD_EXT_UNKNOWN = 0,
    AUD_EXT_MP3,
    AUD_EXT_FLAC,
    AUD_EXT_OPUS,
    AUD_EXT_OGG,
    AUD_EXT_WAV,
    AUD_EXT_MP4,
    AUD_EXT_AAC,
    AUD_EXT_M4A,
    AUD_EXT_TS,
} audio_extension_e;

static audio_extension_e player_get_ext(const char *url) {
    // Search for the extension part of the URL
    const char *ext_ptr = &(url[strlen(url) - 1]);
    while (url != ext_ptr && '.' != *ext_ptr) {
        ext_ptr--;
    }

    // If the url does not contain a period (or the first char is a period) abort!
    if (url == ext_ptr) {
        return AUD_EXT_UNKNOWN;
    }

    // Do a case-insensitive compare against our known extensions
    if (strcasecmp(".mp3", ext_ptr) == 0) {
        return AUD_EXT_MP3;
    } else if (strcasecmp(".mp4", ext_ptr) == 0) {
        return AUD_EXT_MP4;
    } else if (strcasecmp(".flac", ext_ptr) == 0) {
        return AUD_EXT_FLAC;
    } else if (strcasecmp(".opus", ext_ptr) == 0) {
        return AUD_EXT_OPUS;
    } else if (strcasecmp(".ogg", ext_ptr) == 0 || strcasecmp(".oga", ext_ptr) == 0) {
        return AUD_EXT_OGG;
    } else if (strcasecmp(".wav", ext_ptr) == 0) {
        return AUD_EXT_WAV;
    } else if (strcasecmp(".aac", ext_ptr) == 0) {
        return AUD_EXT_AAC;
    } else if (strcasecmp(".m4a", ext_ptr) == 0) {
        return AUD_EXT_M4A;
    } else if (strcasecmp(".ts", ext_ptr) == 0) {
        return AUD_EXT_TS;
    }

    return AUD_EXT_UNKNOWN;
}

BaseType_t player_set_playlist(playlist_operator_handle_t new_playlist, TickType_t ticksToWait) {
    return xQueueSendToBack(s_player_be_queue, &new_playlist, ticksToWait);
}

esp_err_t player_next(void) {
    if (s_playlist == NULL) {
        return ESP_FAIL;
    }
    char *url = NULL;
    audio_pipeline_stop(s_pipeline);
    audio_pipeline_wait_for_stop(s_pipeline);
    audio_pipeline_terminate(s_pipeline);
    if (s_playmode_is_shuffle) {
        uint32_t next_song = esp_random() % s_playlist_len;
        s_pl_oper.choose(s_playlist, next_song, &url);
    } else {
        s_pl_oper.current(s_playlist, &url);
    }
    ESP_LOGI(TAG, "URL: %s", url);
    audio_element_set_uri(s_fs_stream, url);
    audio_pipeline_reset_ringbuffer(s_pipeline);
    audio_pipeline_reset_elements(s_pipeline);
    audio_pipeline_run(s_pipeline);

    return ESP_OK;
}

esp_err_t player_set_shuffle(bool is_shuffle) {
    s_playmode_is_shuffle = is_shuffle;
    return ESP_OK;
}

void player_main(void) {
    s_player_be_queue = xQueueCreate(1, sizeof(playlist_operator_t));

    // create an empty pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(s_pipeline);

    // Initialize the I2S stream
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_config.sample_rate = 48000;
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    s_hp_stream = i2s_stream_init(&i2s_cfg);

    // Initialize the FATFS file reader stream
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    s_fs_stream = fatfs_stream_init(&fatfs_cfg);

    // Loop until we get a valid playlist
    while (s_playlist == NULL) {
        xQueueReceive(s_player_be_queue, &s_playlist, portMAX_DELAY);
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
    audio_element_set_uri(s_fs_stream, url);

    audio_extension_e ext = player_get_ext(url);

    switch (ext) {
        case AUD_EXT_MP3:
            mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
            mp3_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
            s_decode_stream = mp3_decoder_init(&mp3_cfg);
            break;
        case AUD_EXT_FLAC:
            flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
            flac_cfg.out_rb_size = (16 * 1024);
            flac_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
            s_decode_stream  = flac_decoder_init(&flac_cfg);
            break;
        case AUD_EXT_OPUS:
            opus_decoder_cfg_t opus_cfg = DEFAULT_OPUS_DECODER_CONFIG();
            opus_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
            s_decode_stream  = decoder_opus_init(&opus_cfg);
            break;
        case AUD_EXT_OGG:
            ogg_decoder_cfg_t ogg_cfg = DEFAULT_OGG_DECODER_CONFIG();
            ogg_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
            s_decode_stream  = ogg_decoder_init(&ogg_cfg);
            break;
        case AUD_EXT_WAV:
            wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
            wav_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
            s_decode_stream  = wav_decoder_init(&wav_cfg);;
            break;
        case AUD_EXT_MP4:
        case AUD_EXT_AAC:
        case AUD_EXT_M4A:
        case AUD_EXT_TS:
            aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
            aac_cfg.stack_in_ext = PLAYER_DECODE_IN_PSRAM;
            s_decode_stream  = aac_decoder_init(&aac_cfg);
            break;
        default:
            s_decode_stream = NULL;
    }

    // at this point we should have everything we need to start playing!
    // build up the pipeline!
    audio_pipeline_register(s_pipeline, s_fs_stream, "fs");
    audio_pipeline_register(s_pipeline, s_decode_stream, "dec");
    audio_pipeline_register(s_pipeline, s_hp_stream, "hp");

    const char *link_tag[3] = {"fs", "dec", "hp"};
    audio_pipeline_link(s_pipeline, &link_tag[0], 3);

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    audio_pipeline_set_listener(s_pipeline, evt);

    audio_pipeline_run(s_pipeline);

    while (1) {
        audio_event_iface_msg_t msg;
        if (ESP_OK != audio_event_iface_listen(evt, &msg, portMAX_DELAY)) {
            continue;
        }
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
            // Set music info for a new song to be played
            if (msg.source == (void *) s_decode_stream
                && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(s_decode_stream, &music_info);
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
                    if (s_playmode_is_shuffle) {
                        uint32_t next_song = esp_random() % s_playlist_len;
                        s_pl_oper.choose(s_playlist, next_song, &url);
                    } else {
                        s_pl_oper.next(s_playlist, 1, &url);
                    }
                    ESP_LOGI(TAG, "URL: %s", url);
                    audio_element_set_uri(s_fs_stream, url);
                    audio_pipeline_reset_ringbuffer(s_pipeline);
                    audio_pipeline_reset_elements(s_pipeline);
                    audio_pipeline_change_state(s_pipeline, AEL_STATE_INIT);
                    audio_pipeline_run(s_pipeline);
                }
                continue;
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
