typedef enum {
    DS_NO_CHANGE,
    DS_MAIN_MENU,
    DS_NOW_PLAYING,
    DS_BLUETOOTH,
    DS_FILE_EXP,
} disp_state_t;

void ui_set_play(bool is_playing);
void ui_set_volume(uint8_t level);
void ui_set_batt_level(uint8_t level);

void ui_set_top_bar(lv_obj_t *lbl);
void ui_add_style_small(lv_obj_t * obj);
void ui_add_style_big(lv_obj_t * obj);

lv_obj_t *ui_create_top_bar(lv_obj_t *screen);
lv_disp_t *ui_get_display(void);

void ui_common_init(lv_disp_t * disp);
