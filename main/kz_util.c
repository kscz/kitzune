#include <string.h>
#include "kz_util.h"

audio_extension_e kz_get_ext(const char *url) {
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
