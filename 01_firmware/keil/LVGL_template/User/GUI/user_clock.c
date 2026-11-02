/**
 * @file    user_clock.c
 * @brief   真实时钟实现：LSE → RTC，1 秒刷新锁屏与主界面
 */
#include "user_clock.h"

#include <stdio.h>

#include "lvgl.h"
#include "main.h"
#include "ui_lock.h"
#include "ui_main.h"

/* 备份寄存器 0 用作"时间已校准"的标记（掉电由 VBAT 维持；
   FryPi 无电池，所以断电后标记会丢，下次由 PC 重新授时） */
#define CLOCK_BKP_REG   RTC_BKP_DR0
#define CLOCK_BKP_MAGIC 0xA5C3

static RTC_HandleTypeDef s_hrtc;
static bool s_rtc_ok = false;      /* RTC 外设是否初始化成功 */
static bool s_time_set = false;    /* RTC 里是否有有效时间 */

static char s_time_str[8]  = "--:--";
static char s_date_str[8]  = "--/--";

/* ───────────────── RTC 初始化 ───────────────── */

void clock_init(void)
{
    /* ① 打开 PWR 时钟并允许访问备份域（RTC 与备份寄存器都在备份域） */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    /* ② 开启 LSE（板上有 32.768kHz 晶振，PC14/PC15 已由 CubeMX 配好） */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState       = RCC_LSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        s_rtc_ok = false;
        printf("[clock] LSE 启动失败，时钟不可用\r\n");
        return;
    }

    /* ③ 把 RTC 时钟源切到 LSE，并使能 RTC 外设时钟 */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        s_rtc_ok = false;
        printf("[clock] RTC 时钟源切换失败\r\n");
        return;
    }
    __HAL_RCC_RTC_ENABLE();

    /* ④ 初始化 RTC 外设
       LSE = 32768Hz，异步预分频 127、同步预分频 255
       → (127+1) × (255+1) = 32768 → 1Hz 计数 */
    s_hrtc.Instance             = RTC;
    s_hrtc.Init.HourFormat      = RTC_HOURFORMAT_24;
    s_hrtc.Init.AsynchPrediv    = 127;
    s_hrtc.Init.SynchPrediv     = 255;
    s_hrtc.Init.OutPut          = RTC_OUTPUT_DISABLE;
    s_hrtc.Init.OutPutPolarity  = RTC_OUTPUT_POLARITY_HIGH;
    s_hrtc.Init.OutPutType      = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&s_hrtc) != HAL_OK) {
        s_rtc_ok = false;
        printf("[clock] HAL_RTC_Init 失败\r\n");
        return;
    }

    s_rtc_ok = true;

    /* ⑤ 看备份寄存器里的标记，判断时间是否曾经被校准过 */
    s_time_set = (HAL_RTCEx_BKUPRead(&s_hrtc, CLOCK_BKP_REG) == CLOCK_BKP_MAGIC);
    printf("[clock] RTC 就绪，时间已校准=%d\r\n", (int)s_time_set);
}

bool clock_is_set(void)
{
    return s_rtc_ok && s_time_set;
}

/* ───────────────── 设置时间 ───────────────── */

void clock_set(uint16_t year, uint8_t month, uint8_t day,
               uint8_t hour, uint8_t minute, uint8_t second)
{
    if (!s_rtc_ok) return;

    /* 基本合法性检查：不合法就整帧丢弃，避免把 RTC 写成垃圾值 */
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59) {
        printf("[clock] 授时数据非法，忽略\r\n");
        return;
    }

    RTC_TimeTypeDef t = {0};
    t.Hours   = hour;
    t.Minutes = minute;
    t.Seconds = second;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        printf("[clock] 设置时间失败\r\n");
        return;
    }

    RTC_DateTypeDef d = {0};
    d.Year    = (uint8_t)(year % 100);      /* RTC 只存两位年份 */
    d.Month   = month;
    d.Date    = day;
    d.WeekDay = RTC_WEEKDAY_MONDAY;         /* 界面不显示星期，随便填合法值 */
    if (HAL_RTC_SetDate(&s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        printf("[clock] 设置日期失败\r\n");
        return;
    }

    HAL_RTCEx_BKUPWrite(&s_hrtc, CLOCK_BKP_REG, CLOCK_BKP_MAGIC);
    s_time_set = true;
    printf("[clock] 已对时 %02u:%02u:%02u %04u-%02u-%02u\r\n",
           hour, minute, second, year, month, day);
}

/* ───────────────── 读数与格式化 ───────────────── */

const char *clock_time_str(void) { return s_time_str; }
const char *clock_date_str(void) { return s_date_str; }

/** 从 RTC 读一次，刷新两个字符串缓存 */
static void clock_refresh_cache(void)
{
    if (!clock_is_set()) {
        snprintf(s_time_str, sizeof(s_time_str), "--:--");
        snprintf(s_date_str, sizeof(s_date_str), "--/--");
        return;
    }

    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (HAL_RTC_GetTime(&s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return;
    /* ⚠️ 读日期必须在读时间之后、且中间不能停顿，否则 F4 的 RTC 影子寄存器会不同步 */
    if (HAL_RTC_GetDate(&s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return;

    snprintf(s_time_str, sizeof(s_time_str), "%02u:%02u",
             (unsigned)t.Hours, (unsigned)t.Minutes);
    snprintf(s_date_str, sizeof(s_date_str), "%02u/%02u",
             (unsigned)d.Month, (unsigned)d.Date);
}

/* ───────────────── 界面刷新 ───────────────── */

/** 在 LVGL 任务上下文里跑（lv_timer 的安全上下文） */
static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    clock_refresh_cache();
    /* 两个界面都更新；未创建的界面内部会自己判空 */
    ui_main_set_time(s_time_str, s_date_str);
    ui_lock_set_time(s_time_str, s_date_str);
}

void clock_start_ui_timer(void)
{
    clock_refresh_cache();
    ui_main_set_time(s_time_str, s_date_str);
    ui_lock_set_time(s_time_str, s_date_str);
    lv_timer_create(clock_timer_cb, 1000, NULL);   /* 每 1 秒刷新 */
}
