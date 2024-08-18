BaseType_t player_set_playlist(playlist_operator_handle_t new_playlist, TickType_t ticksToWait);
esp_err_t player_playpause(void);
esp_err_t player_next(void);
void player_set_shuffle(bool is_shuffle);
bool player_get_shuffle(void);
void player_main(void);
