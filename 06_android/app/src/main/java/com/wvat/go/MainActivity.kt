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
 * 主界面：连接状态 + 日志 + 两个按钮。
 *
 * 刻意不引入任何 UI 框架（不用 Compose / AppCompat），
 * 全部用系统控件代码构建——构建快、出错点少。
 */
class MainActivity : Activity() {

    private lateinit var statusView: TextView
    private lateinit var logView: TextView
    private lateinit var connectBtn: Button

    private lateinit var link: BtLink
    private lateinit var gateway: GatewayCore
    private lateinit var actions: Actions

    private val logLines = ArrayDeque<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        actions = Actions(this)

        link = BtLink(
            onLine = { line -> gateway.onLine(line) },
            onState = { st ->
                runOnUiThread {
                    statusView.text = st
                    appendLog(st)
                    updateConnectButton()
                    when {
                        // 链路真的通了才启动网关：否则第一帧授时会发进空气里
                        st.startsWith("已连接") -> gateway.start()
                        st.startsWith("链路断开") || st.startsWith("连接失败") -> gateway.stop()
                    }
                }
            },
        )

        gateway = GatewayCore(
            send = { frame -> link.send(frame) },
            actions = actions,
            log = { msg -> runOnUiThread { appendLog(msg) } },
        )

        setContentView(buildUi())
        requestBtPermissions()
    }

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
            text = "把手表通过蓝牙连到这台手机，它就是原来的那台电脑。"
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
                gateway.forceWeather()
            }
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        btnRow.addView(connectBtn)
        btnRow.addView(wxBtn)
        root.addView(btnRow)

        val clearBtn = Button(this).apply {
            text = "清空日志"
            setOnClickListener {
                logLines.clear()
                logView.text = ""
            }
        }
        root.addView(clearBtn)

        logView = TextView(this).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            typeface = android.graphics.Typeface.MONOSPACE
            gravity = Gravity.START
            movementMethod = ScrollingMovementMethod()
        }
        val scroll = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f
            )
            addView(logView)
        }
        root.addView(scroll)

        return root
    }

    // ── 日志 ──────────────────────────────────────────────
    private fun appendLog(msg: String) {
        logLines.addLast(msg)
        while (logLines.size > 200) logLines.removeFirst()
        logView.text = logLines.joinToString("\n")
    }

    private fun dp(v: Int): Int =
        (v * resources.displayMetrics.density).toInt()

    // ── 选设备 ────────────────────────────────────────────
    private fun chooseDevice() {
        if (link.isConnected) {
            gateway.stop()
            link.disconnect()
            updateConnectButton()
            return
        }
        val adapter = BluetoothAdapter.getDefaultAdapter()
        if (adapter == null) {
            appendLog("这台手机没有蓝牙")
            return
        }
        if (!adapter.isEnabled) {
            appendLog("请先打开手机蓝牙")
            return
        }
        if (Build.VERSION.SDK_INT >= 31 &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED
        ) {
            requestBtPermissions()
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
                link.connect(dev)          // 连上后由 onState 启动网关
                updateConnectButton()
            }
            .setNegativeButton("取消", null)
            .show()
    }

    private fun updateConnectButton() {
        connectBtn.text = if (link.isConnected) "断开" else "连接手表"
    }

    // ── 权限 ──────────────────────────────────────────────
    private fun requestBtPermissions() {
        val perms = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            perms += Manifest.permission.BLUETOOTH_CONNECT
            perms += Manifest.permission.BLUETOOTH_SCAN
        }
        if (Build.VERSION.SDK_INT >= 33) {
            perms += Manifest.permission.POST_NOTIFICATIONS
        }
        val todo = perms.filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (todo.isNotEmpty()) {
            requestPermissions(todo.toTypedArray(), 1001)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        gateway.stop()
        link.disconnect()
    }
}
