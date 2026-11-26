package com.nx.android.data

import com.nx.android.BuildConfig
import com.nx.android.R
import com.nx.android.domain.GameSource
import com.nx.android.domain.environment.EnvironmentSetup
import com.nx.android.domain.environment.GuestProcessLauncher
import com.nx.android.data.porting.BuildWorkspaces
import java.io.File

/**
 * Runs the porter and the doctor: the process handling that used to live in the activity, unchanged in its
 * behavior (one stream for stdout and stderr, the porter's events by line, cancel stops the porter and the
 * tools it started). Every run is one thread; the output goes to [onLine] from that thread.
 */
class PortRunner(
    private val environmentSetup: EnvironmentSetup,
    private val guestLauncher: GuestProcessLauncher,
    private val locale: AppLocale,
    private val statusFile: File? = null,
    private val onLine: (String) -> Unit,
) {

    @Volatile private var cancelled = false
    @Volatile private var proc: Process? = null

    fun port(
        gameFile: String, keys: String,
        touch: Boolean, fps60: Boolean, experimental: Boolean,
        aliasHeadroom: Int,
        sharedFont: String = "", firmware: String = "", mtReplay: Boolean = false,
        vulkanDriver: String = "",
        allowDamagedMedia: Boolean = false,
        workspaces: BuildWorkspaces,
        privatePorts: File,
        diagnostics: File,
        installedApks: () -> Map<String, String> = { emptyMap() },
        onEnd: (interrupted: Boolean, cancelled: Boolean) -> Unit,
    ): Thread {
        val args = portArgs(gameFile, keys, touch, fps60, experimental, aliasHeadroom, !BuildConfig.NX_LSFG,
            sharedFont, firmware, mtReplay, deviceLanguage(), vulkanDriver, allowDamagedMedia)
        // A file in a netdisk's folder is not there yet in its middle: the port would read it as a broken dump
        GameSource.cloudFolder(gameFile)?.let { onLine(locale.str(R.string.runner_cloud_folder, it)) }
        return Thread {
            var workspace: File? = null
            var code: Int? = null
            try {
                workspace = workspaces.prepare(gameFile)
                if (!cancelled) code = runGuest(args, mapOf(
                    "NX2A_WORK" to workspace.absolutePath,
                    "NX2A_PREVIOUS_PACK_DIR" to privatePorts.absolutePath,
                    "NX2A_INSTALLED_GAME_APKS" to org.json.JSONObject(installedApks()).toString(),
                ))
            } catch (e: Exception) {
                onLine("[RED] ${e.message}")
            } finally {
                val requestedCancel = cancelled
                val interrupted = !requestedCancel && isInterruptedExit(code)
                if (!interrupted && workspace != null) {
                    workspaces.discard(workspace, diagnostics) { onLine("[RED] $it") }
                }
                onEnd(interrupted, requestedCancel)
            }
        }.also(Thread::start)
    }

    /**
     * The phone's own language: the pack follows it when the game has it (the porter takes it as a wish: a game
     * without it is packed in its own first language, as before).
     */
    private fun deviceLanguage(): String = java.util.Locale.getDefault().toLanguageTag()

    companion object {

        /**
         * The porter's command line for a port: pure, so the test can check it on the JVM.
         *
         * A game installs as its own APK, with a companion folder for arbitrarily large
         * assets. The game's first-run Activity imports the folder using Android's
         * system document picker. The porting tool is not required at runtime.
         *
         * The extra address space (the alias region): Auto passes nothing, the porter decides by itself
         * (the version the profile was tested with gets none, another one 8 GB).
         *
         * The player's own overrides: the system font (a .ttf; empty = the app's own, which has the CJK
         * glyphs), the firmware dump's system content folder (empty = none) and the multithread replay.
         */
        internal fun isInterruptedExit(code: Int?): Boolean = code == 130 || code == 137 || code == 143 || code == 129

        fun portArgs(gameFile: String, keys: String, touch: Boolean, fps60: Boolean,
                     experimental: Boolean, aliasHeadroom: Int, noLsfg: Boolean,
                     sharedFont: String = "", firmware: String = "", mtReplay: Boolean = false,
                     deviceLanguage: String = "", vulkanDriver: String = "", allowDamagedMedia: Boolean = false): List<String> {
            val args = mutableListOf("-m", "porter", "port", gameFile, "-k", keys, "--progress", "json", "--game-apk")
            if (touch) args += "--touch"
            if (fps60) args += "--fps60"
            if (experimental) args += "--experimental"
            if (allowDamagedMedia) args += "--allow-damaged-media"
            if (aliasHeadroom >= 0) args += listOf("--alias-headroom", aliasHeadroom.toString())
            if (sharedFont.isNotBlank()) args += listOf("--shared-font", sharedFont.trim())
            if (firmware.isNotBlank()) args += listOf("--firmware", firmware.trim())
            if (mtReplay) args += "--mt-replay"
            // Legacy build configuration is intentionally ignored: standalone games import drivers at runtime.
            if (deviceLanguage.isNotBlank()) args += listOf("--language-preferred", deviceLanguage.trim())
            // A build without the frame generation (the Gradle property nxLsfg=false): the host the app builds on the
            // device is then smaller and the frame generation reports that it is not in this build (android/NOTICE.md).
            if (noLsfg) args += "--no-lsfg"
            return args
        }
    }

    /**
     * The tools check: the interpreter paths first (the porter would otherwise report its tools missing from the
     * saved setting). [json] asks for the rows as one JSON array, the form the keys check reads.
     */
    fun doctor(keys: String, json: Boolean, onEnd: () -> Unit): Thread {
        val args = mutableListOf("-m", "porter", "doctor")
        if (json) args += "--json"
        if (keys.isNotBlank()) args += listOf("-k", keys.trim())
        val t = Thread {
            try {
                onLine(locale.str(R.string.runner_checking_tools))
                environmentSetup.patchInterpreters()
                onLine(locale.str(R.string.runner_interpreter_patched))
            } catch (e: Exception) {
                onLine(locale.str(R.string.runner_interpreter_patch_failed, e.message))
            }
            start(args, onEnd)
        }
        t.start()
        return t
    }

    private fun start(args: List<String>, onEnd: () -> Unit): Thread = Thread {
        try {
            if (!cancelled) runGuest(args)
        } catch (e: Exception) {
            onLine("[RED] ${e.message}")
        } finally {
            onEnd()
        }
    }.also(Thread::start)

    private fun runGuest(args: List<String>, extra: Map<String, String> = emptyMap()): Int {
        val process = guestLauncher.guest(args, extra = extra)
        proc = process
        try {
            if (cancelled) process.destroyForcibly()
            process.inputStream.bufferedReader().useLines { lines -> lines.forEach(onLine) }
            val code = process.waitFor()
            if (code != 0) onLine(exitLine(code))
            statusFile?.let { writeStatus(it, code) }
            return code
        } finally {
            // A log-reading error must never leave a writer in a workspace being reclaimed.
            if (process.isAlive) {
                process.destroyForcibly()
                process.waitFor()
            }
            proc = null
        }
    }

    /**
     * How the run ended: the exit code, or the signal that killed it (the system takes the memory when a port
     * grows too big; the JDK reports such a process as 128 + the signal).
     */
    private fun exitLine(code: Int): String {
        val sig = signal(code)
        return if (sig != 0) locale.str(R.string.runner_killed, sig) else locale.str(R.string.runner_exit_code, code)
    }

    /** The signal that killed the process, 0 when it ended on its own */
    private fun signal(code: Int): Int = if (code in 129..255) code - 128 else 0

    /**
     * The run's end, in a file: the log bundle takes it, so a port the system killed says so (the porter itself
     * had no chance to write anything).
     */
    private fun writeStatus(file: File, code: Int) {
        try {
            file.parentFile?.mkdirs()
            val sig = signal(code)
            val what = if (sig != 0) "killed by signal $sig" else "exit code $code"
            file.writeText("port: $what\ntime: ${java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(java.util.Date())}\n")
        } catch (e: Exception) {
        }
    }

    /** Stops the porter and the tools it started (they are the app's own processes, stopped one by one) */
    fun cancel() {
        cancelled = true
        try { proc?.destroyForcibly() } catch (e: Exception) {
        }
        proc = null
        val me = android.os.Process.myPid()
        for (d in File("/proc").listFiles() ?: return) {
            if (!d.name.all { it.isDigit() }) continue
            val pid = d.name.toLong()
            if (pid == me.toLong()) continue
            try {
                val cmd = File(d, "cmdline").readText()
                if (cmd.contains("python3.13") || cmd.contains("dotnet") || cmd.contains("ld-linux") ||
                    cmd.contains("clang") || cmd.contains("nx_extract")
                ) {
                    android.os.Process.killProcess(pid.toInt())
                }
            } catch (e: Exception) {
            }
        }
    }
}
