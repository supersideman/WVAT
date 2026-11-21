package com.wvat.go

import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.net.Uri
import android.provider.AlarmClock
import android.provider.Settings
import android.view.KeyEvent

/**
 * 动作执行：把 A01~A08 映射成手机上的真实操作。
 *
 * 平台能力差异（和 PC 网关对照，论文里要写清楚）
 * ------------------------------------------------
 * | 动作 | PC | 手机 |
 * | A01 播放/暂停 | SendInput 媒体键 | dispatchMediaKeyEvent  ✅ |
 * | A02 下一首     | 同上            | 同上                     ✅ |
 * | A03 我困了     | 锁屏+静音+关屏  | 只能静音（关屏要设备管理员）⚠️ |
 * | A04/A05 音量±  | CoreAudio       | AudioManager             ✅ |
 * | A06 打开资料   | 打开文件夹      | 打开"文档"界面           ⚠️ 语义变化 |
 * | A07 查状态     | 读系统音量      | 读音量+天气              ✅ |
 * | A08 语音       | 录音+识别       | 由 App 自己录（二期）    |
 */
class Actions(private val ctx: Context) {

    private val audio: AudioManager
        get() = ctx.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    // ── 媒体键 ────────────────────────────────────────────
    private fun mediaKey(keyCode: Int): Boolean = runCatching {
        val now = android.os.SystemClock.uptimeMillis()
        val down = KeyEvent(now, now, KeyEvent.ACTION_DOWN, keyCode, 0)
        val up = KeyEvent(now, now, KeyEvent.ACTION_UP, keyCode, 0)
        audio.dispatchMediaKeyEvent(down)
        audio.dispatchMediaKeyEvent(up)
        true
    }.getOrDefault(false)

    fun playPause(): String =
        if (mediaKey(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)) "已切换播放" else "媒体键被系统拒绝"

    fun next(): String =
        if (mediaKey(KeyEvent.KEYCODE_MEDIA_NEXT)) "已切到下一首" else "媒体键被系统拒绝"

    fun previous(): String =
        if (mediaKey(KeyEvent.KEYCODE_MEDIA_PREVIOUS)) "已回到上一首" else "媒体键被系统拒绝"

    // ── 音量 ──────────────────────────────────────────────
    fun volumeUp(): String = runCatching {
        audio.adjustStreamVolume(
            AudioManager.STREAM_MUSIC, AudioManager.ADJUST_RAISE, AudioManager.FLAG_SHOW_UI
        )
        "音量 ${volumePercent()}%"
    }.getOrDefault("音量调节失败")

    fun volumeDown(): String = runCatching {
        audio.adjustStreamVolume(
            AudioManager.STREAM_MUSIC, AudioManager.ADJUST_LOWER, AudioManager.FLAG_SHOW_UI
        )
        "音量 ${volumePercent()}%"
    }.getOrDefault("音量调节失败")

    /** 当前媒体音量百分比（0~100）*/
    fun volumePercent(): Int = runCatching {
        val max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
        val cur = audio.getStreamVolume(AudioManager.STREAM_MUSIC)
        cur * 100 / max
    }.getOrDefault(-1)

    fun mute(): String = runCatching {
        audio.adjustStreamVolume(
            AudioManager.STREAM_MUSIC, AudioManager.ADJUST_MUTE, AudioManager.FLAG_SHOW_UI
        )
        "已静音"
    }.getOrDefault("静音失败")

    // ── 我困了：手机上没有"关屏幕"，退化为静音 ────────────
    fun sleepy(): String = mute() + "（手机不支持关屏，已静音）"

    // ── 打开资料：手机上语义变成"打开文档界面" ────────────
    fun openDocuments(): String = runCatching {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        ctx.startActivity(i)
        "已打开文档"
    }.getOrElse {
        // 有些 ROM 没有文档界面，退回到打开展示网页搜索
        runCatching {
            ctx.startActivity(
                Intent(Intent.ACTION_VIEW, Uri.parse("https://www.bing.com"))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
            "已打开浏览器"
        }.getOrElse { "打开失败" }
    }

    // ── 查状态 ────────────────────────────────────────────
    fun status(): String {
        val vol = volumePercent()
        val vText = if (vol < 0) "音量？" else "音量 $vol%"
        return if (Weather.hasData()) {
            val w = when (Weather.kind) {
                0 -> "晴"; 1 -> "多云"; 2 -> "雨"; else -> "雪"
            }
            "$vText · $w ${Weather.temp}℃"
        } else {
            "$vText · 天气未连接"
        }
    }

    // ── 闹钟（给"AI 兜底"用的动作之一）────────────────────
    fun setAlarm(hour: Int, minute: Int, label: String = ""): String = runCatching {
        val i = Intent(AlarmClock.ACTION_SET_ALARM).apply {
            putExtra(AlarmClock.EXTRA_HOUR, hour)
            putExtra(AlarmClock.EXTRA_MINUTES, minute)
            putExtra(AlarmClock.EXTRA_SKIP_UI, true)
            if (label.isNotEmpty()) putExtra(AlarmClock.EXTRA_MESSAGE, label)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        ctx.startActivity(i)
        String.format("已设 %02d:%02d 的闹钟", hour, minute)
    }.getOrElse { "设闹钟失败" }

    // ── 打开指定 App（按包名）─────────────────────────────
    fun openApp(pkg: String): String = runCatching {
        val i = ctx.packageManager.getLaunchIntentForPackage(pkg)
            ?: return "没找到 $pkg"
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        ctx.startActivity(i)
        "已打开 $pkg"
    }.getOrElse { "打开 $pkg 失败" }

    // ── 打开系统设置里的无障碍页（二期用）──────────────────
    fun openAccessibilitySettings(): String = runCatching {
        ctx.startActivity(
            Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        )
        "已打开无障碍设置"
    }.getOrElse { "打开设置失败" }

    /**
     * 按动作码分发。返回给手表的回执文字。
     *
     * 不认识的动作码（或 T 自由文本）返回 null，交给上层走"AI 兜底"。
     */
    fun run(code: String): String? = when (code) {
        "A01" -> playPause()
        "A02" -> next()
        "A03" -> sleepy()
        "A04" -> volumeUp()
        "A05" -> volumeDown()
        "A06" -> openDocuments()
        "A07" -> status()
        "AP"  -> previous()
        "MUTE" -> mute()
        "DOC" -> openDocuments()
        else -> null
    }
}
