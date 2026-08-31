
lv_obj_t *ui_np_get_screen(void);
esp_err_t ui_np_init(void);

void ui_np_set_song_title(const char *title);
void ui_np_set_time(int elapsed_sec, int total_sec);
disp_state_t ui_np_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle);
