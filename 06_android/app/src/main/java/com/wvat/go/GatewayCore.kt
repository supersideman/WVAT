package com.wvat.go

import android.os.Handler
import android.os.Looper
import java.util.Calendar

/**
 * 网关核心：Mobile 版 02_gateway/gateway.py。
 *
 * 职责（和 PC 网关一一对应）
 * --------------------------
 * 1. 授时：连上就发 OV+EV=TIME，之后每 30 秒一次（手表 RTC 断电会归零）
 * 2. 心跳：每 20 秒 OV+PING；连续 3 次没 PONG 判掉线（并清 REQ_ID 去重表）
 * 3. 天气：等手表先说过话（证明链路双向通）再推，之后每 5 分钟
 * 4. 指令：OV+AI=<id>,<code> → 执行 → 回 OV+AR
 * 5. 兜底：认不出的指令（含 T 自由文本）交给 AI（二期接入）
 *
 * 线程模型：所有周期任务跑在主线程 Handler 上（很轻），
 * 天气请求自己开子线程，完成后 post 回来发送。
 */
class GatewayCore(
    private val send: (String) -> Boolean,
    private val actions: Actions,
    private val log: (String) -> Unit,
) {
    companion object {
        private const val TICK_MS = 500L
        private const val TIME_SYNC_MS = 30_000L
        private const val HEARTBEAT_MS = 20_000L
        private const val WX_MS = 300_000L
        private const val MAX_MISSED_PONG = 3
        private const val DEDUP_WINDOW = 8
    }

    private val handler = Handler(Looper.getMainLooper())
    private var running = false

    private var lastTimeSync = 0L
    private var lastPing = 0L
    private var lastWx = 0L
    private var missedPong = 0
    private var sawDevice = false

    private var lastReqId = 0
    private val seenReqIds = ArrayDeque<Int>()

    val isRunning: Boolean get() = running

    fun start() {
        if (running) return
        running = true
        lastTimeSync = 0L
        lastPing = 0L
        lastWx = 0L
        missedPong = 0
        sawDevice = false
        lastReqId = 0
        seenReqIds.clear()
        handler.post(tick)
        log("网关已启动：连上即授时 · 20 秒心跳 · 5 分钟天气")
    }

    fun stop() {
        running = false
        handler.removeCallbacks(tick)
        log("网关已停止")
    }

    // ── 周期任务 ──────────────────────────────────────────
    private val tick = object : Runnable {
        override fun run() {
            if (!running) return
            val now = System.currentTimeMillis()

            // ① 授时
            if (now - lastTimeSync >= TIME_SYNC_MS) {
                lastTimeSync = now
                sendTime()
            }

            // ② 心跳
            if (now - lastPing >= HEARTBEAT_MS) {
                lastPing = now
                send(OvProtocol.ping())
                missedPong++
                if (missedPong > MAX_MISSED_PONG) {
                    log("[!] 心跳超时：手表可能断电或复位（清空去重表）")
                    missedPong = 0
                    lastReqId = 0
                    seenReqIds.clear()
                }
            }

            // ③ 天气：必须等手表先说过话，否则可能把帧发进"只收不发"的链路
            if (sawDevice && now - lastWx >= WX_MS) {
                lastWx = now
                pushWeather()
            }

            handler.postDelayed(this, TICK_MS)
        }
    }

    private fun sendTime() {
        val c = Calendar.getInstance()
        val frame = OvProtocol.timeEvent(
            c.get(Calendar.HOUR_OF_DAY),
            c.get(Calendar.MINUTE),
            c.get(Calendar.SECOND),
            c.get(Calendar.YEAR),
            c.get(Calendar.MONTH) + 1,
            c.get(Calendar.DAY_OF_MONTH),
        )
        send(frame)
    }

    private fun pushWeather() {
        Weather.update(WX_MS) {
            if (!Weather.hasData()) {
                log("天气取失败：${Weather.lastError}")
                return@update
            }
            handler.post {
                send(OvProtocol.wxEvent(Weather.kind, Weather.temp))
                log("→ 天气 类别${Weather.kind} ${Weather.temp}℃")
            }
        }
    }

    /** 手动触发一次天气推送（界面上有个按钮，方便调试） */
    fun forceWeather() {
        lastWx = System.currentTimeMillis()
        Weather.update(WX_MS, force = true) {
            if (Weather.hasData()) {
                handler.post {
                    send(OvProtocol.wxEvent(Weather.kind, Weather.temp))
                    log("→ 天气（手动）类别${Weather.kind} ${Weather.temp}℃")
                }
            } else {
                handler.post { log("天气取失败：${Weather.lastError}") }
            }
        }
    }

    // ── 收到一行 ──────────────────────────────────────────
    fun onLine(line: String) {
        val frame = OvProtocol.parse(line)
        if (frame == null) {
            log("设备: $line")     // 固件的 printf 日志走同一条串口
            return
        }
        when (frame.kind) {
            OvProtocol.KIND_PONG -> {
                missedPong = 0
                sawDevice = true
            }
            OvProtocol.KIND_PING -> send(OvProtocol.pong())

            OvProtocol.KIND_REQ -> {
                sawDevice = true
                onRequest(frame)
            }
            else -> Unit
        }
    }

    private fun onRequest(frame: OvProtocol.Frame) {
        val id = frame.reqId
        val code = frame.code

        // 去重：设备重传同一个 REQ_ID 时不重复执行
        if (id in seenReqIds) {
            log("↺ 忽略重复请求 #$id")
            return
        }
        seenReqIds.addLast(id)
        while (seenReqIds.size > DEDUP_WINDOW) seenReqIds.removeFirst()
        lastReqId = id

        log("← #$id $code${if (frame.arg.isNotEmpty()) " ${frame.arg}" else ""}")

        // 自由文本 / 未知动作 → 交给 AI 兜底（二期）
        val result = if (code == "T" || code.isBlank()) null else actions.run(code)
        if (result == null) {
            val why = if (code == "T") "AI 兜底未接入" else "不支持的动作 $code"
            send(OvProtocol.ack(id, false, why))
            log("→ ERR $why")
            return
        }

        send(OvProtocol.ack(id, true, result))
        log("→ OK $result")
    }
}
