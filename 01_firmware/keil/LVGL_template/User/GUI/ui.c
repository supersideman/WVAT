
#include "ui.h"
#include "ui_main.h"
#include "user_link.h"
#include "ui_stage.h"
#include "ui_lock.h"

///////////////////// VARIABLES ////////////////////

// IMAGES AND IMAGE SETS

///////////////////// TEST LVGL SETTINGS ////////////////////
#if LV_COLOR_DEPTH != 16
    #error "LV_COLOR_DEPTH should be 16bit to match SquareLine Studio's settings"
#endif

///////////////////// ANIMATIONS ////////////////////

///////////////////// FUNCTIONS ////////////////////

///////////////////// SCREENS ////////////////////

void ui_init(void)
{
    lv_disp_t * dispp = lv_disp_get_default();
    lv_theme_t * theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                               true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    ui_stage_init();
    lv_disp_load_scr(ui_stage_screen());
    link_init();   /* Æô¶¯ USART1 Ö¸ÁîÁ´Â· */
}

void ui_destroy(void)
{
    ui_HomePage_screen_destroy();
}
