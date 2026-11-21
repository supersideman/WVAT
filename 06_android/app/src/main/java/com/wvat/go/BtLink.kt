package com.wvat.go

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.PrintWriter
import java.util.UUID
import kotlin.concurrent.thread

/**
 * HC-05 的串口链路（经典蓝牙 SPP）。
 *
 * 要点
 * ----
 * - **波特率由 HC-05 决定**（出厂 9600）。手机侧没有"波特率"这个概念，
 *   两侧速率不匹配时表现是"收到乱码"，改 HC-05 的 AT+BAUDx 即可。
 * - 连接必须放在子线程：createRfcommSocketToServiceRecord().connect() 是阻塞的，
 *   最长十几秒，放主线程会 ANR。
 * - 读也是子线程：readLine() 会一直阻塞到有数据。
 * - 断线要能自己发现：读线程抛异常/返回 null 就当作断开，通知上层重连。
 */
class BtLink(
    private val onLine: (String) -> Unit,
    private val onState: (String) -> Unit,
) {
    companion object {
        /** SPP 的标准 UUID，所有串口蓝牙模块（HC-05/HC-06/JDY）都用它 */
        val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    }

    @Volatile private var socket: BluetoothSocket? = null
    @Volatile private var writer: PrintWriter? = null
    @Volatile private var running = false
    private var readerThread: Thread? = null
    private var connectThread: Thread? = null

    val isConnected: Boolean
        get() = socket?.isConnected == true

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        disconnect()
        running = true
        onState("连接中…")

        connectThread = thread(name = "bt-connect", isDaemon = true) {
            try {
                // 搜索会严重拖慢握手，先取消
                runCatching { BluetoothAdapter.getDefaultAdapter()?.cancelDiscovery() }

                // 先试标准 SPP；个别模块（山寨 HC-05）只认通道 1，失败再退回去
                val s = try {
                    device.createRfcommSocketToServiceRecord(SPP_UUID)
                } catch (e: Exception) {
                    val m = device.javaClass.getMethod("createRfcommSocket", Int::class.javaPrimitiveType)
                    m.invoke(device, 1) as BluetoothSocket
                }
                s.connect()                       // 阻塞，最长约 12 秒
                socket = s
                writer = PrintWriter(s.outputStream.writer(), false)
                onState("已连接：${device.name ?: device.address}")
                startReader(s)
            } catch (e: Exception) {
                onState("连接失败：${e.javaClass.simpleName} ${e.message ?: ""}")
                running = false
                runCatching { socket?.close() }
                socket = null
            }
        }
    }

    private fun startReader(s: BluetoothSocket) {
        readerThread = thread(name = "bt-read", isDaemon = true) {
            try {
                val br = BufferedReader(InputStreamReader(s.inputStream, Charsets.UTF_8))
                while (running) {
                    val line = br.readLine() ?: break
                    if (line.isNotEmpty()) onLine(line)
                }
            } catch (_: Exception) {
                // 断开、被拔电、模块掉线都会走到这里
            } finally {
                val wasRunning = running
                running = false
                runCatching { writer?.close() }
                writer = null
                runCatching { socket?.close() }
                socket = null
                if (wasRunning) onState("链路断开")
            }
        }
    }

    /** 发一帧（已含 \r\n）。返回 false 表示当前没有链路 */
    fun send(line: String): Boolean {
        val w = writer ?: return false
        return try {
            w.print(line)
            w.flush()
            true
        } catch (_: Exception) {
            false
        }
    }

    fun disconnect() {
        running = false
        runCatching { writer?.close() }
        writer = null
        runCatching { socket?.close() }
        socket = null
        readerThread?.interrupt()
        readerThread = null
        connectThread?.interrupt()
        connectThread = null
        onState("已断开")
    }
}
