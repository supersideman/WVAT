
#include "ui.h"
#include "ui_main.h"
#include "user_link.h"
#include "ui_stage.h"
#include "user_clock.h"
#include "user_i2s.h"
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
    link_init();   /* 启动 USART1 指令链路 */
    clock_init();            /* 开 LSE，初始化内部 RTC */
    clock_start_ui_timer();  /* 每 1 秒刷新锁屏与主界面 */
    mic_boot_selftest();     /* I2S 麦克风自检：串口打印 [mic] */
}

void ui_destroy(void)
{
    /* 作者示例界面已从工程移除，这里不再需要销毁 */
}
