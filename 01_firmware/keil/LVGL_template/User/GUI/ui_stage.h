/**
 * @file    ui_stage.h
 * @brief   舞台：把「锁屏层」和「主界面层」叠在一个 240×560 的容器里，
 *          拖动时整体跟手移动，实现"向上滑 → 主界面从下面跟着上来"。
 *
 * 为什么这么做（而不是用两个屏幕 + 转场动画）：
 *   LVGL 的 lv_scr_load_anim 是"固定时长的动画"，手指松开的位移无法参与控制，
 *   滑起来像"按一下就跳过去"。做成一个可拖动容器后，位移与手指 1:1 对应，
 *   手感才对；再把松手后的吸附交给 lv_anim 补完。
 */
#ifndef UI_STAGE_H
#define UI_STAGE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化舞台（只调用一次）：建屏幕 + 两个层 + 绑定拖拽事件 */
void ui_stage_init(void);

/** 取舞台所在的屏幕对象（开机 load 用） */
lv_obj_t *ui_stage_screen(void);

/** 立即切到"已解锁"状态（从抽屉等其它屏幕返回主界面时用）*/
void ui_stage_snap_home(void);

/** 切到主界面并播放转场动画（抽屉返回用）*/
void ui_stage_show_home(lv_scr_load_anim_t anim);

/** 回到锁屏（上滑锁屏 / 超时锁屏时用）*/
void ui_stage_lock(bool animate);

#ifdef __cplusplus
}
#endif

#endif /* UI_STAGE_H */
