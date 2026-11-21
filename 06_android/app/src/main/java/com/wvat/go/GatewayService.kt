package com.wvat.go

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder

/**
 * 前台服务：让网关在 App 转后台、锁屏后继续跑。
 *
 * 为什么必须有它
 * --------------
 * 普通 App 转后台后随时可能被系统回收；一旦进程没了，蓝牙连接和定时器
 * （授时/心跳/天气）全部停止，手表就"连不上手机"了。
 * 前台服务带一条常驻通知，把进程提到前台优先级，系统不会随手杀。
 *
 * 注意（Android 14+ / API 34+）
 * ----------------------------
 * 声明 foregroundServiceType="connectedDevice" 时，
 * 必须有 FOREGROUND_SERVICE_CONNECTED_DEVICE 权限，
 * 并且已获得 BLUETOOTH_CONNECT 之类的连接类权限。
 */
class GatewayService : Service() {

    companion object {
        private const val CH_ID = "wvat_gateway"
        private const val NOTIF_ID = 1001

        @Volatile private var instance: GatewayService? = null

        fun start(ctx: Context) {
            val i = Intent(ctx, GatewayService::class.java)
            try {
                if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i)
                else ctx.startService(i)
            } catch (e: Exception) {
                Gateway.log("前台服务启动失败：${e.javaClass.simpleName} ${e.message ?: ""}")
            }
        }

        fun stop(ctx: Context) {
            try {
                ctx.stopService(Intent(ctx, GatewayService::class.java))
            } catch (_: Exception) {
            }
        }

        /** 更新常驻通知上的状态文字 */
        fun update(text: String) {
            instance?.setText(text)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        instance = this
        createChannel()
        startForeground(NOTIF_ID, build("网关运行中"))
        Gateway.log("前台服务已启动（转后台不会被杀）")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // 被系统杀掉后自动重建；真正的连接要靠用户重新点连接
        return START_STICKY
    }

    override fun onDestroy() {
        instance = null
        Gateway.log("前台服务已停止")
        super.onDestroy()
    }

    private fun setText(text: String) {
        try {
            val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.notify(NOTIF_ID, build(text))
        } catch (_: Exception) {
        }
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT < 26) return
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val ch = NotificationChannel(
            CH_ID, "网关运行状态", NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "显示手表连接状态，关闭它会断开网关"
            setShowBadge(false)
        }
        nm.createNotificationChannel(ch)
    }

    private fun build(text: String): Notification {
        val b = if (Build.VERSION.SDK_INT >= 26)
            Notification.Builder(this, CH_ID)
        else
            @Suppress("DEPRECATION") Notification.Builder(this)

        return b.setContentTitle("WVAT Go 网关")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true)
            .build()
    }
}
