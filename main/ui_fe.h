
lv_obj_t *ui_fe_get_screen(void);
lv_obj_t *ui_fe_get_top_bar(void);
esp_err_t ui_fe_init(void);
disp_state_t ui_fe_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle);
