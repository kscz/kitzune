#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"

// Pull in ALL THE DECODERS!
#include "mp3_decoder.h"
#include "amr_decoder.h"
#include "opus_decoder.h"
#include "ogg_decoder.h"
#include "flac_decoder.h"
#include "wav_decoder.h"
#include "aac_decoder.h"

#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "board.h"

#define PLAYER_DECODE_IN_PSRAM (true)

// internal queue used to manage new playlists
static QueueHandle_t s_player_be_queue = NULL;

void player_main(void) {
    s_player_be_queue = xQueueCreate(1, sizeof(playlist_handle_t));
}
