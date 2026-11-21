/**
 * @file    ui_date.h
 * @brief   日期页（三页里最左边那页）：大日期 + 星期 + 未来内容的加号占位
 */
#ifndef UI_DATE_H
#define UI_DATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 打开日期页（从主界面向右滑进入）*/
void ui_date_open(void);

/** 关闭日期页（向左滑或点底部条回主界面）*/
void ui_date_close(void);

/** 日期页是否正开着 */
bool ui_date_visible(void);

/**
 * 更新日期文字（由 user_clock 的 1 秒定时器调用）。
 * @param day  "21"        日（大号数字）
 * @param ym   "2026/11"   年/月
 * @param wd   "FRI"       英文星期缩写
 * 页面还没创建时会被安全忽略。
 */
void ui_date_set_text(const char *day, const char *ym, const char *wd);

#ifdef __cplusplus
}
#endif

#endif /* UI_DATE_H */
