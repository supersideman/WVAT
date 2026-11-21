package com.wvat.go

import android.bluetooth.BluetoothDevice
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * 网关单例：把 BtLink / GatewayCore / Actions 的生命周期从 Activity 里拿出来。
 *
 * 为什么要单例
 * ------------
 * 做成前台服务以后，Activity 可能被系统回收（转后台、内存吃紧），
 * 但网关必须继续跑。如果这些对象挂在 Activity 上，Activity 一没，
 * 引用就断了（而且再进 App 会新建一套，造成两条链路打架）。
 * 放在 object 里，进程活着网关就活着，界面只是"观察者"。
 *
 * 线程约定
 * --------
 * - BtLink 的读写各自有线程，回调可能来自任意线程
 * - 所有 UI 通知统一 post 到主线程
 * - state/connected 用 @Volatile，界面随时读
 */
object Gateway {

    private const val TAG = "WVAT"

    private val main = Handler(Looper.getMainLooper())

    /** 界面注册的日志观察者；Activity 走 onStart/onStop 挂上/摘掉 */
    var logListener: ((String) -> Unit)? = null
        set(value) {
            field = value
            // 重新挂上时把最近的状态补一条，避免界面看起来"没反应"
            if (value != null) value("状态：$state")
        }

    private var actions: Actions? = null
    private var link: BtLink? = null
    private var core: GatewayCore? = null

    @Volatile var state: String = "未连接"
        private set
    @Volatile var connected: Boolean = false
        private set
    @Volatile var deviceName: String = ""
        private set

    /** 统一日志出口：系统日志 + 界面（如果有观察者）*/
    fun log(msg: String) {
        Log.i(TAG, msg)
        val l = logListener
        if (l != null) main.post { l(msg) }
    }

    fun isConnected(): Boolean = connected

    /**
     * 建立连接并启动网关。调用方（Activity）负责先拿到已配对的 BluetoothDevice。
     * 连接成功后会自动拉起前台服务，保证转后台不被杀。
     */
    fun connect(ctx: android.content.Context, device: BluetoothDevice) {
        disconnect()                       // 先收干净，避免两条链路并存

        actions = Actions(ctx.applicationContext)

        val l = BtLink(
            onLine = { line -> core?.onLine(line) },
            onState = { st ->
                state = st
                main.post { logListener?.invoke(st) }
                when {
                    st.startsWith("已连接") -> {
                        connected = true
                        deviceName = device.name ?: device.address
                        core?.start()
                        GatewayService.start(ctx)       // 前台服务：保活
                        GatewayService.update("已连接 $deviceName")
                    }
                    st.startsWith("链路断开") || st.startsWith("连接失败") -> {
                        connected = false
                        core?.stop()
                        GatewayService.stop(ctx)
                    }
                }
            },
        )
        link = l

        core = GatewayCore(
            send = { frame -> link?.send(frame) ?: false },
            actions = actions!!,
            log = { msg -> log(msg) },
        )

        state = "连接中…"
        l.connect(device)
    }

    fun disconnect() {
        core?.stop()
        link?.disconnect()
        core = null
        link = null
        connected = false
        state = "已断开"
    }

    /** 手动推一次天气（界面上的调试按钮）*/
    fun forceWeather() {
        core?.forceWeather() ?: log("还没连接，无法推天气")
    }
}
