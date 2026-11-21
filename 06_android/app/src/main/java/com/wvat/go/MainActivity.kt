package com.wvat.go

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * 主界面：只做"显示 + 操作入口"，真正的网关在 [Gateway] 单例里跑。
 *
 * 这样设计的结果：转后台、锁屏、Activity 被回收，网关都不受影响；
 * 再回到 App 时把日志观察者重新挂上就能接着看。
 */
class MainActivity : Activity() {

    private lateinit var statusView: TextView
    private lateinit var logView: TextView
    private lateinit var connectBtn: Button

    private val logLines = ArrayDeque<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildUi())
        requestPermissionsIfNeeded()
    }

    override fun onStart() {
        super.onStart()
        // 重新挂上日志观察者（切换回 App 时能看到之前的状态）
        Gateway.logListener = { msg -> appendLog(msg) }
        statusView.text = Gateway.state
        updateConnectButton()
    }

    override fun onStop() {
        super.onStop()
        Gateway.logListener = null      // 界面不在前台就别再往它上面推了
    }

    // ───────────────── UI ─────────────────

    private fun buildUi(): ViewGroup {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
        }

        root.addView(TextView(this).apply {
            text = "WVAT Go · 手机网关"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
        })

        root.addView(TextView(this).apply {
            text = "手表通过蓝牙连这台手机；转后台后由前台服务保活。"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setPadding(0, dp(4), 0, dp(12))
        })

        statusView = TextView(this).apply {
            text = "未连接"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            setPadding(0, 0, 0, dp(8))
        }
        root.addView(statusView)

        val btnRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        connectBtn = Button(this).apply {
            text = "连接手表"
            setOnClickListener { chooseDevice() }
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        val wxBtn = Button(this).apply {
            text = "推天气"
            setOnClickListener {
                appendLog("手动触发天气：${Weather.city}")
                Gateway.forceWeather()
            }
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        btnRow.addView(connectBtn)
        btnRow.addView(wxBtn)
        root.addView(btnRow)

        root.addView(Button(this).apply {
            text = "清空日志"
            setOnClickListener {
                logLines.clear()
                logView.text = ""
            }
        })

        logView = TextView(this).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            typeface = android.graphics.Typeface.MONOSPACE
            gravity = Gravity.START
            movementMethod = ScrollingMovementMethod()
        }
        root.addView(ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f
            )
            addView(logView)
        })

        return root
    }

    private fun appendLog(msg: String) {
        logLines.addLast(msg)
        while (logLines.size > 200) logLines.removeFirst()
        logView.text = logLines.joinToString("\n")
        statusView.text = Gateway.state
        updateConnectButton()
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun updateConnectButton() {
        connectBtn.text = if (Gateway.isConnected()) "断开" else "连接手表"
    }

    // ───────────────── 选设备 ─────────────────

    private fun chooseDevice() {
        if (Gateway.isConnected()) {
            Gateway.disconnect()
            GatewayService.stop(this)
            appendLog("已手动断开")
            updateConnectButton()
            return
        }

        val adapter = BluetoothAdapter.getDefaultAdapter()
        if (adapter == null) {
            appendLog("这台设备没有蓝牙")
            return
        }
        if (!adapter.isEnabled) {
            appendLog("请先打开手机蓝牙")
            return
        }
        if (Build.VERSION.SDK_INT >= 31 &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissionsIfNeeded()
            appendLog("请先授予蓝牙权限，再点一次连接")
            return
        }

        val devices: List<BluetoothDevice> = adapter.bondedDevices.toList()
        if (devices.isEmpty()) {
            appendLog("没有已配对的设备：先在系统设置里配对 HC-05")
            return
        }

        val names = devices.map { "${it.name ?: "(无名)"}\n${it.address}" }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("选择已配对的蓝牙设备")
            .setItems(names) { _, which ->
                val dev = devices[which]
                appendLog("连接 ${dev.name ?: dev.address} …")
                Gateway.connect(this, dev)   // 连上后由 Gateway 拉起前台服务
                updateConnectButton()
            }
            .setNegativeButton("取消", null)
            .show()
    }

    // ───────────────── 权限 ─────────────────

    private fun requestPermissionsIfNeeded() {
        val perms = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            perms += Manifest.permission.BLUETOOTH_CONNECT
            perms += Manifest.permission.BLUETOOTH_SCAN
        }
        if (Build.VERSION.SDK_INT >= 33) {
            perms += Manifest.permission.POST_NOTIFICATIONS
        }
        val todo = perms.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (todo.isNotEmpty()) requestPermissions(todo.toTypedArray(), 1001)
    }
}
