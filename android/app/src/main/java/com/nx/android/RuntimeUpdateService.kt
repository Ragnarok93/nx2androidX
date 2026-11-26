package com.nx.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import androidx.core.content.ContextCompat
import com.nx.android.appstate.LibraryState
import com.nx.android.appstate.PortState
import com.nx.android.appstate.RuntimeUpdateState
import com.nx.android.data.AppLocale
import com.nx.android.data.AppLog
import com.nx.android.data.PrefsStore
import com.nx.android.data.UpgradeApks
import com.nx.android.data.firmware.FirmwareRepository
import com.nx.android.data.porting.BuildWorkspaces
import com.nx.android.data.RuntimeCompiler
import com.nx.android.data.RuntimeUpdateResult
import dagger.hilt.android.AndroidEntryPoint
import javax.inject.Inject

/** Keeps the modal runtime update alive when the app is backgrounded. It never creates a port task. */
@AndroidEntryPoint
class RuntimeUpdateService : Service() {
    @Inject lateinit var state: RuntimeUpdateState
    @Inject lateinit var compiler: RuntimeCompiler
    @Inject lateinit var library: LibraryState
    @Inject lateinit var locale: AppLocale
    @Inject lateinit var upgradeApks: UpgradeApks
    @Inject lateinit var prefs: PrefsStore
    @Inject lateinit var firmware: FirmwareRepository
    @Inject lateinit var port: PortState
    @Inject lateinit var workspaces: BuildWorkspaces
    private var working = false

    companion object {
        private const val CHANNEL = "nx2a_runtime_update"
        fun start(context: Context) {
            ContextCompat.startForegroundService(context, Intent(context, RuntimeUpdateService::class.java))
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (working) return START_NOT_STICKY
        if (!state.running) {
            stopSelf(startId)
            return START_NOT_STICKY
        }
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL,
            locale.str(R.string.runtime_update), NotificationManager.IMPORTANCE_LOW))
        val tap = PendingIntent.getActivity(this, 2,
            Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP).apply {
                if (state.apkUpgrade) putExtra(MainActivity.EXTRA_TAB, 2)
            },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        startForeground(2, Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(locale.str(R.string.runtime_update))
            .setContentText(state.name)
            .setProgress(0, 0, true)
            .setOngoing(true)
            .setContentIntent(tap)
            .build())
        working = true
        val pack = state.pack
        val apkUpgrade = state.apkUpgrade
        val request = state.apkRequest
        val keys = prefs.keys
        val firmwarePath = firmware.folder.orEmpty()
        val config = request?.game?.base?.absolutePath?.let { base ->
            port.history.firstOrNull { it.game == base && it.config != null }?.config
        }
        // 玩家点了「用当前档案继续」时这一轮才带上这个开关（第一次不带：照旧失败并说清原因）。
        // 这个选择只对这一次有效：别的重试照旧按档案核对。
        val allowDrift = state.takeDrift()
        val handler = Handler(mainLooper)
        Thread {
            val result = if (apkUpgrade) {
                runCatching {
                    checkNotNull(request) { "Missing APK upgrade target." }
                    check(request.staging.mkdirs()) { "Cannot create an upgrade staging folder in Downloads." }
                    compiler.upgradeApk(request, keys, firmwarePath, config, workspaces, upgradeApks) { line ->
                        AppLog.line(line)
                        handler.post { state.appendApkLog(line) }
                    }
                }.fold(
                    onSuccess = { completed ->
                        handler.post { state.completeApk(completed.apk, completed.companion) }
                        RuntimeUpdateResult(true)
                    },
                    onFailure = { error ->
                        request?.let {
                            runCatching { upgradeApks.removeInterruptedStage(it.staging.absolutePath) }
                                .onFailure { AppLog.line("[RED] Upgrade staging cleanup failed: ${it.message}") }
                        }
                        AppLog.line("[RED] APK upgrade failed: ${error.message}")
                        RuntimeUpdateResult(false, error.message.orEmpty())
                    },
                ).also { request?.cancellation?.delete() }
            } else compiler.rebuild(pack, allowProfileDrift = allowDrift)
            handler.post {
                working = false
                state.finish(result.success, result.reason, result.profileDrift)
                if (!apkUpgrade) library.rescan()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }.start()
        return START_NOT_STICKY
    }
}
