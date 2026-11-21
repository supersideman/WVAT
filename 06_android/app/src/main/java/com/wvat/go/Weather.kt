package com.wvat.go

import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

/**
 * 天气：与 02_gateway/weather.py 用同一个数据源（open-meteo，免费、免 key）。
 *
 * 手表没网，天气只能由手机取回来再推给手表（OV+EV=WX,<类别>,<温度>）。
 * 类别只有 4 个：0=晴 1=多云 2=雨 3=雪（雷雨归雨、雾归多云），
 * 因为手表的图标就是按这 4 类画的图元。
 */
object Weather {

    /** 当前天气类别；-1 表示还没有数据 */
    @Volatile var kind: Int = -1
        private set
    @Volatile var temp: Int = 0
        private set
    @Volatile var lastError: String = ""
        private set

    /** 城市坐标（改这两个值即可换城市）*/
    var lat: Double = 39.9042
    var lon: Double = 116.4074
    var city: String = "北京"

    private const val TIMEOUT_MS = 8000
    private const val FAIL_BACKOFF_MS = 300_000L   // 失败后 5 分钟再试

    @Volatile private var lastTry = 0L
    @Volatile private var fetching = false

    fun hasData(): Boolean = kind >= 0

    /** WMO 天气代码 → 手表能画的 4 类 */
    fun wmoToKind(code: Int): Int = when {
        code == 0 -> 0
        code == 1 || code == 2 || code == 3 || code == 45 || code == 48 -> 1
        code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86 -> 3
        else -> 2
    }

    /**
     * 到点就异步刷新一次。
     * @param intervalSec 最小间隔（秒）
     * @param force       true = 忽略间隔，立刻取
     * @param onDone      取完（无论成败）在子线程回调
     */
    fun update(intervalSec: Long, force: Boolean = false, onDone: (() -> Unit)? = null) {
        val now = System.currentTimeMillis()
        if (fetching && !force) return
        if (!force) {
            if (now - lastTry < intervalSec * 1000) return
            if (lastError.isNotEmpty() && now - lastTry < FAIL_BACKOFF_MS) return
        }
        fetching = true
        lastTry = now

        thread(name = "weather", isDaemon = true) {
            var conn: HttpURLConnection? = null
            try {
                val url = URL(
                    "https://api.open-meteo.com/v1/forecast" +
                        "?latitude=$lat&longitude=$lon" +
                        "&current=temperature_2m,weather_code&timezone=auto"
                )
                // 用非空的局部变量，避免可空 var 的智能转换问题
                val c = (url.openConnection() as HttpURLConnection).apply {
                    connectTimeout = TIMEOUT_MS
                    readTimeout = TIMEOUT_MS
                    setRequestProperty("User-Agent", "WVATGo/0.1")
                }
                conn = c
                val text = c.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
                val cur = JSONObject(text).getJSONObject("current")
                val code = cur.getInt("weather_code")
                val t = cur.getDouble("temperature_2m")

                kind = wmoToKind(code)
                temp = Math.round(t).toInt()
                lastError = ""
            } catch (e: Exception) {
                // 网络问题不该影响其它功能：只记一行原因，界面显示上次的值
                lastError = "${e.javaClass.simpleName}: ${e.message ?: ""}"
            } finally {
                runCatching { conn?.disconnect() }
                fetching = false
                onDone?.invoke()
            }
        }
    }
}
