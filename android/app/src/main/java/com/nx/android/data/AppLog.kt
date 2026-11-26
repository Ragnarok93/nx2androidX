package com.nx.android.data

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File

/**
 * The app's own log: everything the port and the tools say also lands in a file (files/nx2a/log.txt), so it can
 * be read off the phone (or shared out of Settings) when something went wrong.
 */
object AppLog {

    private const val CAPTURE_LIMIT = 256 * 1024
    private val capture = StringBuilder()
    var captureActive by mutableStateOf(false)
        private set

    @Synchronized
    fun startCapture() {
        capture.clear()
        captureActive = true
    }

    @Synchronized
    fun stopCapture(): String {
        captureActive = false
        return capture.toString()
    }

    private var file: File? = null
    private var writer: java.io.BufferedWriter? = null

    fun init(ctx: Context) {
        if (file != null) return
        init(File(ctx.filesDir, "nx2a/log.txt"))
    }

    /** The log file itself: the app's own directory in the app, a temp file in the tests */
    @Synchronized
    fun init(f: File) {
        try { writer?.close() } catch (e: Exception) {}
        writer = null
        f.parentFile?.mkdirs()
        file = f
    }

    @Synchronized
    fun line(s: String) {
        if (captureActive) {
            val line = if (s.length >= CAPTURE_LIMIT) "[builder capture: oversized message omitted]" else s
            val overflow = capture.length + line.length + 1 - CAPTURE_LIMIT
            if (overflow > 0) {
                val newline = capture.indexOf("\n", overflow - 1)
                capture.delete(0, if (newline < 0) capture.length else newline + 1)
            }
            capture.appendLine(line)
        }
        val f = file ?: return
        try {
            val w = writer ?: java.io.BufferedWriter(java.io.FileWriter(f, true)).also { writer = it }
            w.write(s + "\n")
            w.flush()
        } catch (e: Exception) {
        }
    }

    /** The log file itself: the bundle takes it as it is */
    fun logFile(): File? = file

    /** The log's text (the tail, for sharing) */
    fun text(maxLines: Int = 400): String {
        val f = file ?: return ""
        if (!f.exists()) return ""
        return try {
            f.readLines().takeLast(maxLines).joinToString("\n")
        } catch (e: Exception) {
            ""
        }
    }

    @Synchronized
    fun clear() {
        capture.clear()
        try { writer?.close() } catch (e: Exception) {}
        writer = null
        file?.delete()
    }
}
