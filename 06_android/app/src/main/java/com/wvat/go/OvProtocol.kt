package com.wvat.go

/**
 * OV+ 协议：与 02_gateway/protocol.py 和 01_firmware/ui/user_link.c 完全对齐。
 *
 * 帧格式（都是 ASCII，以 \r\n 结尾）：
 *   手表 → App   OV+AI=<id>,<code>[,<arg>]      意图请求（A01…A08 或 T,<文字>）
 *   App → 手表   OV+AR=<id>,<OK|ERR>[,<text>]   回执
 *   App → 手表   OV+EV=TIME,<HH:MM:SS>,<YYYY-MM-DD>   授时
 *   App → 手表   OV+EV=WX,<类别>,<温度>               天气（类别 0晴 1多云 2雨 3雪）
 *   App → 手表   OV+EV=ASR,<文字>                语音识别结果（二期）
 *   双向         OV+PING / OV+PONG              心跳
 *
 * 为什么协议一字不改：手表固件里已经有完整实现，
 * 手机端只要"像原来那台电脑一样说话"，手表端零改动。
 */
object OvProtocol {

    private const val TERM = "\r\n"

    const val KIND_REQ = "AI"
    const val KIND_ACK = "AR"
    const val KIND_EVT = "EV"
    const val KIND_PING = "PING"
    const val KIND_PONG = "PONG"

    /** 回执文字上限（与固件 LINK_LINE_MAX 96 留出余量） */
    const val MAX_TEXT_BYTES = 60

    /** 解析后的一帧 */
    class Frame(val kind: String, val body: String) {
        /** OV+AI=<id>,... 里的 id；解析不出返回 -1 */
        val reqId: Int
            get() = body.substringBefore(',').trim().toIntOrNull() ?: -1

        /** 动作码，例如 A01 / T / start */
        val code: String
            get() = body.split(',').getOrNull(1)?.trim() ?: ""

        /** 第三个字段起的全部内容（自由文本里可能含逗号，不能简单 split 丢）
         *  注意：手表发的 T,<文字> 形如 "5,T,打开资料"，
         *  code 已经是 "T"，arg 就是文字本身。 */
        val arg: String
            get() {
                val parts = body.split(',')
                return if (parts.size > 2) parts.drop(2).joinToString(",") else ""
            }
    }

    /** 去掉回车换行并截断，避免把帧结构搞坏 */
    fun clean(text: String): String =
        text.replace('\r', ' ').replace('\n', ' ').take(MAX_TEXT_BYTES)

    fun timeEvent(
        hour: Int, minute: Int, second: Int,
        year: Int, month: Int, day: Int,
    ): String = String.format(
        "OV+EV=TIME,%02d:%02d:%02d,%04d-%02d-%02d%s",
        hour, minute, second, year, month, day, TERM,
    )

    fun wxEvent(kind: Int, temp: Int): String = "OV+EV=WX,$kind,$temp$TERM"

    fun asrEvent(text: String): String = "OV+EV=ASR,${clean(text)}$TERM"

    fun ping(): String = "OV+$KIND_PING$TERM"

    fun pong(): String = "OV+$KIND_PONG$TERM"

    fun ack(reqId: Int, ok: Boolean, text: String = ""): String =
        "OV+$KIND_ACK=$reqId,${if (ok) "OK" else "ERR"},${clean(text)}$TERM"

    /** 解析一行；不认识就返回 null（固件的 printf 日志会走同一条串口）*/
    fun parse(line: String): Frame? {
        val s = line.trim()
        if (!s.startsWith("OV+")) return null
        val rest = s.substring(3)
        val eq = rest.indexOf('=')
        val kind = if (eq < 0) rest else rest.substring(0, eq)
        val body = if (eq < 0) "" else rest.substring(eq + 1)
        return when (kind) {
            KIND_REQ, KIND_ACK, KIND_EVT, KIND_PING, KIND_PONG -> Frame(kind, body)
            else -> null
        }
    }
}
