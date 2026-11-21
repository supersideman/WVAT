/**
 * @file    ui_stage.c
 * @brief   舞台实现：跟手滑动解锁 · LVGL 8.2
 */
#include "ui_stage.h"

#include <stdint.h>

#include "ui_lock.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "user_backlight.h"

/* 调试开关：把内部状态从 USART1 打出来（printf 已在 usart.c 重定向）
 * 排查阶段用 1；平时设 0——调试日志会占串口带宽，也会混进网关日志。 */
#define UI_STAGE_DEBUG 0
#if UI_STAGE_DEBUG
#include <stdio.h>
#define DBG(...)  printf(__VA_ARGS__)
#else
#define DBG(...)  do {} while (0)
#endif

#define COL(c) lv_color_hex(c)

#define STAGE_H   (UI_H * 2)

static lv_obj_t *s_root  = NULL;   /* 屏幕 240×280 */
static lv_obj_t *s_stage = NULL;   /* 容器 240×560：上=锁屏，下=主界面 */
static lv_obj_t *s_lock  = NULL;   /* 锁屏层（拖拽事件挂这）*/

static int  s_press_y  = -1;       /* 按下时的屏幕 y，-1 表示没按 */
static bool s_unlocked = false;

/* ───────────────── 位置控制 ───────────────── */

/** y ∈ [-UI_H, 0]：0=锁屏，-UI_H=主界面 */
static void stage_set_y(int y)
{
    if (y > 0) y = 0;
    if (y < -UI_H) y = -UI_H;
    lv_obj_set_y(s_stage, y);
    DBG("[stage] set_y=%d (readback=%d)\r\n", y, (int)lv_obj_get_y(s_stage));
}

static void anim_cb(void *var, int32_t v)
{
    (void)var;
    lv_obj_set_y(s_stage, v);
}

static void stage_animate_to(int target)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_stage);
    lv_anim_set_exec_cb(&a, anim_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_stage), target);
    lv_anim_set_time(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ───────────────── 手势 ───────────────── */

static void unlock_commit(void)
{
    s_unlocked = true;
    s_press_y = -1;
    stage_animate_to(-UI_H);
}

static void on_pressed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (s_unlocked) return;                  /* 已解锁就别再拖了，否则会跟按钮抢事件 */
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_press_y = p.y;
    lv_anim_del(s_stage, anim_cb);           /* 手指按下时打断正在跑的吸附动画 */
}

/** 跟手：手指移动多少，容器就移动多少 */
static void on_pressing(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSING) return;
    if (s_unlocked || s_press_y < 0) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = s_press_y - p.y;                /* 向上为正 */
    if (dy < 0) dy = 0;                      /* 往下拖不动，防止把锁屏拖出屏幕 */
    stage_set_y(-dy);
}

static void on_released(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (s_unlocked || s_press_y < 0) return;

    int dragged = -lv_obj_get_y(s_stage);    /* 已经拖上去了多少像素 */
    s_press_y = -1;

    if (dragged >= UI_LOCK_SWIPE_PX) {
        unlock_commit();
    } else {
        stage_animate_to(0);                 /* 不够就弹回去 */
    }
}

/** 兜底：长按也能解锁（防手势失灵被卡住）*/
static void on_long_pressed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
    if (s_unlocked) return;
    unlock_commit();
}

/* ───────────────── 对外接口 ───────────────── */

void ui_stage_init(void)
{
    if (s_root) return;

    /* 背光调光：必须在 LCD 初始化之后（这里就是），
       会把 lcd_init 设成 GPIO 输出的 PA15 改成 TIM2_CH1 PWM */
    bl_init();

    s_root = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_root, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    /* 舞台容器：自己不能点，否则会挡住下层的点击 */
    s_stage = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_stage);
    lv_obj_set_pos(s_stage, 0, 0);
    lv_obj_set_size(s_stage, UI_W, STAGE_H);
    lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 上：锁屏层
       ⚠️ 顺序必须"先 build、再 set_pos"：lv_obj_set_pos/set_size 写的是样式属性，
       而 build 内部的 lv_obj_remove_style_all() 会把刚设的坐标一起清掉 → 回到 (0,0)。
       踩过的坑：主界面层被 reset 到 y=0 盖住锁屏（复位直接就是主界面），
       返回时两层一起被推出屏幕外（黑屏）。*/
    s_lock = lv_obj_create(s_stage);
    ui_lock_build(s_lock);
    lv_obj_set_pos(s_lock, 0, 0);

    /* 下：主界面层 */
    lv_obj_t *home = lv_obj_create(s_stage);
    ui_main_build(home);
    lv_obj_set_pos(home, 0, UI_H);           /* build 之后再定位置！*/

    /* 拖拽事件挂在锁屏层上：已解锁时手指落在主界面层，自然不会触发 */
    lv_obj_add_flag(s_lock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lock, on_pressed,  LV_EVENT_PRESSED,      NULL);
    lv_obj_add_event_cb(s_lock, on_pressing, LV_EVENT_PRESSING,     NULL);
    lv_obj_add_event_cb(s_lock, on_released, LV_EVENT_RELEASED,     NULL);
    lv_obj_add_event_cb(s_lock, on_long_pressed, LV_EVENT_LONG_PRESSED, NULL);

    s_unlocked = false;
    stage_set_y(0);

    /* 关键几何信息。
       ⚠️ lv_obj_get_width/get_y 读的是"计算后的坐标"，必须先跑一次 layout，
       否则全是 0（第一版调试就被这个假 0 误导过）。*/
    lv_obj_update_layout(s_root);
    DBG("\r\n[stage] === init done ===\r\n");
    DBG("[stage] screen: w=%d h=%d\r\n",
        (int)lv_obj_get_width(s_root), (int)lv_obj_get_height(s_root));
    DBG("[stage] stage : x=%d y=%d w=%d h=%d\r\n",
        (int)lv_obj_get_x(s_stage), (int)lv_obj_get_y(s_stage),
        (int)lv_obj_get_width(s_stage), (int)lv_obj_get_height(s_stage));
    DBG("[stage] lock  : x=%d y=%d w=%d h=%d\r\n",
        (int)lv_obj_get_x(s_lock), (int)lv_obj_get_y(s_lock),
        (int)lv_obj_get_width(s_lock), (int)lv_obj_get_height(s_lock));
}

lv_obj_t *ui_stage_screen(void)
{
    if (!s_root) ui_stage_init();
    return s_root;
}

void ui_stage_snap_home(void)
{
    if (!s_stage) return;
    lv_anim_del(s_stage, anim_cb);
    s_unlocked = true;
    s_press_y = -1;
    stage_set_y(-UI_H);
}

void ui_stage_show_home(lv_scr_load_anim_t anim)
{
    DBG("[stage] show_home: anim=%d, 切换前 y=%d\r\n", (int)anim, (int)lv_obj_get_y(s_stage));
    ui_stage_snap_home();                     /* 先摆到"已解锁"位置，再切屏 */
    lv_scr_load_anim(ui_stage_screen(), anim, 220, 0, false);
    DBG("[stage] show_home: 切屏后 y=%d\r\n", (int)lv_obj_get_y(s_stage));
}

void ui_stage_lock(bool animate)
{
    if (!s_stage) return;
    s_unlocked = false;
    s_press_y = -1;
    if (animate) stage_animate_to(0);
    else         stage_set_y(0);
}
