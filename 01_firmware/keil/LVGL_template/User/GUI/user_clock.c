/**
 * @file    user_clock.c
 * @brief   真实时钟：直接操作 STM32F4 的 RTC 寄存器（不依赖 HAL_RTC 模块）
 *
 * 为什么不用 HAL_RTC_xxx？
 *   作者模板里没有配置 RTC，所以：
 *     · stm32f4xx_hal_conf.h 里 HAL_RTC_MODULE_ENABLED 被注释掉
 *     · Drivers/.../Src 下根本没有 stm32f4xx_hal_rtc.c / _rtc_ex.c
 *     · 仓库里 OV_Watch 工程虽然有这两个文件，但它的 HAL 版本与模板不同
 *   混入不同版本的 HAL 模块风险更大；而本项目对 RTC 的需求只有
 *   "开 LSE、设预分频、读写时间日期、用一个备份寄存器存标记"，
 *   直接写寄存器既短又可控。
 *
 * 硬件事实：FryPi 板上有 32.768kHz 晶振（.ioc 里 PC14/PC15 =
 *   LSE-External-Oscillator），但模板的 SystemClock_Config 只开了 HSE，
 *   LSE 从未被使能——这里自己打开。
 */
#include "user_clock.h"

#include <stdio.h>

#include "lvgl.h"
#include "main.h"
#include "ui_lock.h"
#include "ui_main.h"

/* RTC 写保护解锁/上锁的固定魔数（参考 RM0383 §26.3.5）*/
#define RTC_KEY1  0xCAu
#define RTC_KEY2  0x53u
#define RTC_LOCK  0xFFu

/* 备份寄存器 0 里存"时间已校准"的标记 */
#define CLOCK_BKP_MAGIC 0xA5C3u

static bool s_rtc_ok = false;
static bool s_time_set = false;

static char s_time_str[8] = "--:--";
static char s_date_str[8] = "--/--";

/* ───────────────── 小工具 ───────────────── */

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

static void rtc_unlock(void)
{
    RTC->WPR = RTC_KEY1;
    RTC->WPR = RTC_KEY2;
}

static void rtc_lock(void)
{
    RTC->WPR = RTC_LOCK;
}

/** 进入初始化模式并写预分频；LSE=32768 → (127+1)*(255+1)=32768 → 1Hz */
static bool rtc_enter_init(void)
{
    RTC->ISR |= RTC_ISR_INIT;
    uint32_t timeout = 0x00100000u;
    while ((RTC->ISR & RTC_ISR_INITF) == 0u) {
        if (--timeout == 0u) return false;
    }
    RTC->PRER = (127u << 16) | 255u;
    return true;
}

static void rtc_exit_init(void)
{
    RTC->ISR &= ~RTC_ISR_INIT;
}

/* ───────────────── 初始化 ───────────────── */

void clock_init(void)
{
    /* ① 使能 PWR 时钟并允许访问备份域（RTC 与备份寄存器都在备份域）*/
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;                 /* 读一次，保证时钟稳定生效 */
    PWR->CR |= PWR_CR_DBP;

    /* ② 判断 RTC 是否已由固件配置过（看 BDCR 的时钟源与使能位）*/
    bool configured = ((RCC->BDCR & RCC_BDCR_RTCEN) != 0u) &&
                      ((RCC->BDCR & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_0);

    if (!configured) {
        printf("[clock] 首次配置 RTC，启动 LSE...\r\n");

        /* 打开 LSE 外部晶振并等待起振 */
        RCC->BDCR |= RCC_BDCR_LSEON;
        uint32_t timeout = 0x00100000u;
        while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0u) {
            if (--timeout == 0u) {
                printf("[clock] LSE 起振超时，时钟不可用（界面显示 --:--）\r\n");
                return;
            }
        }

        /* RTC 时钟源选 LSE（RTCSEL = 01），并使能 RTC */
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_0;
        RCC->BDCR |= RCC_BDCR_RTCEN;

        /* 首次配置要写预分频：解锁 → 进初始化模式 → 退出 → 上锁 */
        rtc_unlock();
        if (!rtc_enter_init()) {
            rtc_lock();
            printf("[clock] RTC 初始化超时\r\n");
            return;
        }
        rtc_exit_init();
        rtc_lock();
    } else {
        printf("[clock] RTC 已配置过，直接使用\r\n");
    }

    s_rtc_ok = true;
    s_time_set = (RTC->BKP0R == CLOCK_BKP_MAGIC);
    printf("[clock] RTC 就绪，时间已校准=%d\r\n", (int)s_time_set);
}

bool clock_is_set(void)
{
    return s_rtc_ok && s_time_set;
}

/* ───────────────── 写时间 ───────────────── */

/** 新时间与当前 RTC 时间是否已足够接近（避免每 30 秒重复写 + 刷日志）*/
static bool clock_almost_same(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (!clock_is_set()) return false;

    uint32_t tr = RTC->TR;
    uint8_t s = bcd2bin((uint8_t)(tr & 0x7Fu));
    uint8_t m = bcd2bin((uint8_t)((tr >> 8) & 0x7Fu));
    uint8_t h = bcd2bin((uint8_t)((tr >> 16) & 0x3Fu));

    int32_t a = (int32_t)hour * 3600 + (int32_t)minute * 60 + (int32_t)second;
    int32_t b = (int32_t)h * 3600 + (int32_t)m * 60 + (int32_t)s;
    int32_t d = a - b;
    if (d < 0) d = -d;
    if (d > 12 * 3600) d = 24 * 3600 - d;      /* 跨午夜也算接近 */
    return d <= 2;
}

void clock_set(uint16_t year, uint8_t month, uint8_t day,
               uint8_t hour, uint8_t minute, uint8_t second)
{
    if (!s_rtc_ok) return;

    if (month < 1u || month > 12u || day < 1u || day > 31u ||
        hour > 23u || minute > 59u || second > 59u) {
        printf("[clock] 授时数据非法，忽略\r\n");
        return;
    }

    /* 网关每 30 秒授时一次；时间没跑偏就什么都不做，避免无谓写入与刷屏 */
    if (clock_almost_same(hour, minute, second)) {
        return;
    }

    rtc_unlock();
    if (!rtc_enter_init()) {
        rtc_lock();
        printf("[clock] 进入初始化模式失败\r\n");
        return;
    }

    /* BCD 编码写入：TR = [时][分][秒]，DR = [年][月][日] */
    RTC->TR = ((uint32_t)bin2bcd(hour)   << 16) |
              ((uint32_t)bin2bcd(minute) << 8)  |
               (uint32_t)bin2bcd(second);
    RTC->DR = ((uint32_t)bin2bcd((uint8_t)(year % 100u)) << 16) |
              ((uint32_t)bin2bcd(month)                  << 8)  |
               (uint32_t)bin2bcd(day);

    rtc_exit_init();
    rtc_lock();

    RTC->BKP0R = CLOCK_BKP_MAGIC;        /* 标记"时间已校准" */
    s_time_set = true;

    printf("[clock] 已对时 %02u:%02u:%02u %04u-%02u-%02u\r\n",
           hour, minute, second, year, month, day);
}

/* ───────────────── 读时间与格式化 ───────────────── */

const char *clock_time_str(void) { return s_time_str; }
const char *clock_date_str(void) { return s_date_str; }

static void clock_refresh_cache(void)
{
    if (!clock_is_set()) {
        snprintf(s_time_str, sizeof(s_time_str), "--:--");
        snprintf(s_date_str, sizeof(s_date_str), "--/--");
        return;
    }

    /* 先读 TR 再读 DR：F4 会锁存影子寄存器，保证时间与日期取自同一瞬间 */
    uint32_t tr = RTC->TR;
    uint32_t dr = RTC->DR;

    uint8_t sec = bcd2bin((uint8_t)(tr & 0x7Fu));
    uint8_t min = bcd2bin((uint8_t)((tr >> 8) & 0x7Fu));
    uint8_t hr  = bcd2bin((uint8_t)((tr >> 16) & 0x3Fu));
    uint8_t day = bcd2bin((uint8_t)(dr & 0x3Fu));
    uint8_t mon = bcd2bin((uint8_t)((dr >> 8) & 0x1Fu));

    (void)sec;                                  /* 界面只显示到分钟 */
    snprintf(s_time_str, sizeof(s_time_str), "%02u:%02u", hr, min);
    snprintf(s_date_str, sizeof(s_date_str), "%02u/%02u", mon, day);
}

/* ───────────────── 界面刷新 ───────────────── */

/** 在 LVGL 任务上下文里跑，所以可以安全地改控件 */
static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    clock_refresh_cache();
    ui_main_set_time(s_time_str, s_date_str);
    ui_lock_set_time(s_time_str, s_date_str);
}

void clock_start_ui_timer(void)
{
    clock_refresh_cache();
    ui_main_set_time(s_time_str, s_date_str);
    ui_lock_set_time(s_time_str, s_date_str);
    lv_timer_create(clock_timer_cb, 1000, NULL);   /* 每 1 秒刷新一次 */
}
