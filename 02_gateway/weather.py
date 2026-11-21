"""天气：从 open-meteo 取"当前天气"，供网关周期性发给手表。

为什么放在网关而不是手表
------------------------
手表没有网络（只有 USB / 蓝牙两条短链路），所以天气必须由电脑取回来、
再通过协议推给手表：
    OV+EV=WX,<天气代码>,<温度>
例如  OV+EV=WX,2,14      表示"多云，14 ℃"

为什么用 open-meteo
-------------------
* 免费、**不需要 API key**（省掉注册和泄露密钥的麻烦）
* 只要经纬度就能查，返回里自带 WMO weather_code，映射简单
* 用标准库 urllib 就能请求，不引入新依赖

注意
----
* 默认坐标是**北京**，请按自己所在城市改 LAT / LON（见文件末尾）
* 取失败时保留上一次成功的值，界面上不会突然变成空白
* 网络问题只打印一行提示，不影响网关其它功能
"""
from __future__ import annotations

import json
import time
import urllib.request

# ── 配置：改成你所在城市的经纬度 ────────────────────────────
LAT = 39.9042      # 纬度（北京）
LON = 116.4074     # 经度
CITY = "北京"      # 仅用于日志

FETCH_TIMEOUT = 8.0     # 单次 HTTP 超时（秒）。国内访问境外接口偶尔偏慢，给足时间
FETCH_RETRY = 2         # 一次失败后再试 1 次（共 2 次尝试）
FAIL_BACKOFF = 300.0    # 失败后至少隔 5 分钟再试，避免断网时反复请求+刷屏

# 想换数据源只需改这一行（保持返回 JSON 里有 current.temperature_2m / weather_code 即可）
API_URL = ("https://api.open-meteo.com/v1/forecast"
           "?latitude={lat}&longitude={lon}"
           "&current=temperature_2m,weather_code&timezone=auto")


def wmo_to_kind(code: int) -> int:
    """把 WMO 天气代码归成手表能画的 4 类。

    返回：0=晴 1=多云 2=雨 3=雪（雷雨按雨画，雾按多云画）
    """
    if code == 0:
        return 0
    if code in (1, 2, 3, 45, 48):
        return 1
    if code in (71, 73, 75, 77, 85, 86):
        return 3
    # 51~67 毛毛雨/雨，80~82 阵雨，95~99 雷雨
    return 2


class Weather:
    """带缓存与退避的天气取值器（只在网关主循环里被调用，单线程）。"""

    def __init__(self, lat: float = LAT, lon: float = LON, city: str = CITY):
        self.lat = lat
        self.lon = lon
        self.city = city
        self.code = None        # 原始 WMO 码
        self.temp = None        # 整数摄氏度
        self.last_ok = 0.0      # 上次成功时间
        self.last_try = 0.0     # 上次尝试时间
        self.last_error = ""

    # ── 内部：真正发请求 ────────────────────────────────
    def _fetch_once(self) -> bool:
        url = API_URL.format(lat=self.lat, lon=self.lon)
        req = urllib.request.Request(url, headers={"User-Agent": "OV-Watch/1.0"})
        with urllib.request.urlopen(req, timeout=FETCH_TIMEOUT) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        cur = data.get("current") or {}
        code = cur.get("weather_code")
        temp = cur.get("temperature_2m")
        if code is None or temp is None:
            self.last_error = "返回里缺少 weather_code / temperature_2m"
            return False
        self.code = int(code)
        self.temp = int(round(float(temp)))
        self.last_ok = time.time()
        self.last_error = ""
        return True

    def _fetch(self) -> bool:
        """带重试的取数：境外接口偶尔握手超时，试一次往往就好了。"""
        for i in range(FETCH_RETRY):
            try:
                return self._fetch_once()
            except Exception as e:                   # 网络/解析问题都不该拖垮网关
                self.last_error = f"{type(e).__name__}: {e}"
                if i + 1 < FETCH_RETRY:
                    time.sleep(1.0)
        return False

    # ── 对外：需要时刷新，返回 (kind, temp) 或 None ──────
    def update(self, min_interval: float) -> bool:
        """到点就刷新一次。返回 True 表示这次拿到了新数据。"""
        now = time.time()
        if now - self.last_try < min_interval:
            return False
        # 上次失败过就先退避，避免断网时反复重试
        if self.last_error and now - self.last_try < FAIL_BACKOFF:
            return False

        self.last_try = now
        ok = self._fetch()
        if ok:
            print(f"[weather] {self.city} {self.temp}℃ "
                  f"(WMO {self.code} → 类别 {wmo_to_kind(self.code)})", flush=True)
        else:
            print(f"[weather] 取天气失败：{self.last_error}", flush=True)
        return ok

    def frame_fields(self):
        """给协议用的两个字段；还没成功过就返回 None。"""
        if self.code is None or self.temp is None:
            return None
        return wmo_to_kind(self.code), self.temp


# 想要别的城市：把下面几行换成你的经纬度（百度/高德搜"XX 经纬度"就行）
#   上海 31.2304, 121.4737
#   广州 23.1291, 113.2644
#   成都 30.5728, 104.0668
