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

audio_extension_e kz_get_ext(const char *url);
