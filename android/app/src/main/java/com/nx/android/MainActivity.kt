package com.nx.android

import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.lifecycleScope
import dagger.hilt.android.AndroidEntryPoint
import com.nx.android.domain.porting.PortPhase
import javax.inject.Inject
import androidx.activity.compose.setContent
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import com.nx.android.R
import com.nx.android.appstate.EnvCheckState
import com.nx.android.appstate.OnboardingState
import com.nx.android.appstate.PortState
import com.nx.android.data.AppLocale
import com.nx.android.data.AppLog
import com.nx.android.data.AppVersion
import com.nx.android.data.LogBundle
import com.nx.android.data.PrefsStore
import com.nx.android.data.porting.BuildSessionRepository
import com.nx.android.core.storage.AppPaths
import androidx.core.content.FileProvider
import com.nx.android.domain.diagnosticVariables
import com.nx.android.app.AppActions
import com.nx.android.app.AppRoot
import java.io.File
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * The shell: it owns what only an activity can own — the storage permission, the system pickers — and puts
 * the screen up. Hilt supplies long-lived dependencies; screens receive state holders
 * and action lambdas, never this activity.
 */
@AndroidEntryPoint
class MainActivity : ComponentActivity() {

    @Inject lateinit var portCompletionCoordinator: com.nx.android.appstate.porting.PortCompletionCoordinator

    companion object {
        const val EXTRA_TAB = "tab"   // the notification's tap lands on the builds page
        const val EXTRA_BUILD_ID = "buildId"

    }

    @Inject lateinit var locale: AppLocale
    @Inject lateinit var prefs: PrefsStore
    @Inject lateinit var operations: com.nx.android.appstate.LibraryOperationCoordinator
    @Inject lateinit var runtime: com.nx.android.appstate.RuntimeUpdateState
    @Inject lateinit var port: PortState
    @Inject lateinit var buildSessions: BuildSessionRepository
    @Inject lateinit var buildWorkspaces: com.nx.android.data.porting.BuildWorkspaces
    @Inject lateinit var paths: AppPaths
    @Inject lateinit var onboarding: OnboardingState
    @Inject lateinit var environmentSetup: com.nx.android.domain.environment.EnvironmentSetup
    @Inject lateinit var drivers: com.nx.android.domain.drivers.DriverRepository
    @Inject lateinit var firmwareRepo: com.nx.android.data.firmware.FirmwareRepository
    @Inject lateinit var envCheck: EnvCheckState

    /** 界面语言：toast 与分享面板的标题从这里取词 */
    /** Which tab the screen opens on (the notification's tap says the builds page) */
    var launchTab by mutableStateOf(1)
    private var launchBuildId by mutableStateOf("")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        onboarding.storageOk = storageGranted()
        onboarding.batteryOk = batteryUnrestricted()
        launchTab = intent?.getIntExtra(EXTRA_TAB, 1) ?: 1
        launchBuildId = intent?.getStringExtra(EXTRA_BUILD_ID).orEmpty()
        if (!prefs.bool(PrefsStore.OB_COMPLETED)) onboarding.open()
        AppLog.init(this)
        port.attach()   // a port the system killed shows up as interrupted
        lifecycleScope.launch(Dispatchers.IO) {
            buildWorkspaces.reconcile { AppLog.line("[RED] $it") }
        }
        port.history.filter { !it.legacy && it.phase in setOf(PortPhase.DONE, PortPhase.FAILED, PortPhase.CANCELLED, PortPhase.INTERRUPTED) }
            .forEach(portCompletionCoordinator::complete)
        if (port.phase == PortPhase.INTERRUPTED)
            port.history.firstOrNull { it.phase == PortPhase.INTERRUPTED }?.let(portCompletionCoordinator::complete)
        setContent {
            AppRoot(prefs, locale, port, onboarding, drivers, environmentSetup, actions,
                launchTab, runtime, launchBuildId)
        }
        // the small tools come with the app: an update brings new ones, and the phone has to get them
        // (the app's own Vulkan driver too: it is unpacked into the app's folder, ready to be picked)
        Thread {
            environmentSetup.refreshTools()
        }.start()
        lifecycleScope.launch {
            drivers.ensureShipped()
            drivers.refresh()
        }

    }

    /** The screens' way back to the activity's own privileges: the actions they may ask for, nothing else */
    private val actions = AppActions(
        onAskStorage = { askStorage() },
        onAskBattery = { askBattery() },
        storageOk = { storageGranted() },
        onSetKeys = { prefs.writeKeys(it); envCheck.refresh() },
        onStartPort = { file, cfg ->
            // 没有「所有文件访问」权限就读不到游戏文件（重装 App 后会掉）：移植会死在半个 Python 栈里，先说清楚
            if (port.running || runtime.running) {
                toast(locale.str(R.string.runtime_busy))
            } else if (file.canRead()) {
                // Export a standalone APK beside the source game; no library pack directory is created.
                PortService.start(this, file.absolutePath, prefs.keys,
                                  cfg.touch, cfg.fps60, cfg.experimental, cfg.aliasHeadroom,
                                  cfg.sharedFont, firmwareRepo.folder ?: "", cfg.mtReplay, allowDamagedMedia = cfg.allowDamagedMedia)
            } else {
                toast(locale.str(R.string.builds_needs_storage)); askStorage()
            }
        },
        onCancelPort = { PortService.cancel(this) },
        onExportApk = { apk -> exportApk(apk) },
        onUpgradeApk = { apk, companion ->
            if (!environmentSetup.ready) {
                toast(locale.str(R.string.upgrade_prepare_environment))
            } else if (!apk.isFile || !apk.canRead()) {
                toast(locale.str(R.string.builds_needs_storage))
                askStorage()
            } else if (!operations.beginApkUpgrade(apk.absolutePath, companion?.absolutePath.orEmpty())) {
                toast(locale.str(R.string.runtime_busy))
            } else {
                try {
                    RuntimeUpdateService.start(this)
                } catch (error: Exception) {
                    runtime.finish(false, error.message.orEmpty())
                }
            }
        },
        onToast = { toast(it) },
        version = { versionName() },
        keys = { prefs.keys },
        // 固件：整机级的一份，导入 zip 解到 App 的私有目录（可选项）
        firmware = { firmwareRepo.imported },
        onImportFirmware = { zip -> importFirmware(zip) },
        onClearFirmware = {
            firmwareRepo.clear()
            envCheck.refresh()   // the chip's colour follows it at once
            toast(locale.str(R.string.fw_cleared))
        },
        // 文件管理器能不能看见 App 的私有目录（运行时、存档、日志）。文档提供器自己读这份设置。
        fileProvider = { prefs.bool(PrefsStore.FILE_PROVIDER, true) },
        onSetFileProvider = { on -> prefs.put(PrefsStore.FILE_PROVIDER, on) },
        onShareLog = {
            lifecycleScope.launch {
                val history = port.history
                val capture = AppLog.stopCapture()
                val diagnostics = variableDiagnostics(history) + listOfNotNull(
                    capture.takeIf { it.isNotBlank() }?.let { "builder-capture.txt" to it },
                    runtime.apkLog.takeIf { runtime.apkUpgrade && it.isNotEmpty() }
                        ?.let { "upgrade.log" to it.joinToString("\n") },
                )
                val zip = withContext(Dispatchers.IO) {
                    LogBundle.build(this@MainActivity, paths.work, buildSessions.logFiles(), bundleDir(),
                        diagnostics = diagnostics, sensitiveValues = sensitiveVariables(history),
                        preservedStages = buildSessions.preservedStageLogs() + history
                            .filter { it.phase == PortPhase.RUNNING || it.phase == PortPhase.INTERRUPTED }
                            .distinctBy { it.game }.flatMap(::unfinishedStageLogs))
                }
                shareBundle(zip, R.string.main_share_log, R.string.main_log_empty)
            }
        },
        onClearLog = {
            AppLog.clear()
            toast(locale.str(R.string.main_log_cleared))
        },
        readLog = { AppLog.text(2000) },
        onShareBuildLog = { id ->
            lifecycleScope.launch {
                val history = port.history
                val session = history.firstOrNull { it.id == id }
                when {
                    session == null || !session.logAvailable -> toast(locale.str(R.string.builds_log_unavailable))
                    buildSessions.logLength(id) == 0L -> toast(locale.str(R.string.builds_log_empty))
                    else -> {
                        val safeName = BuildSessionRepository.safeName(session.game)
                        val log = buildSessions.logFile(id)
                        val zip = withContext(Dispatchers.IO) {
                            LogBundle.build(this@MainActivity, paths.work,
                                listOfNotNull(log?.let { "$safeName-${id.take(8)}.log" to it }),
                                bundleDir(), "$safeName-${id.take(8)}", variableDiagnostics(listOf(session)),
                                sensitiveVariables(history), buildSessions.preservedStageLogs(id) +
                                    unfinishedStageLogs(session))
                        }
                        shareBundle(zip, R.string.builds_log_share, R.string.builds_log_empty)
                    }
                }
            }
        },
        readBuildLog = { id, offset, length -> buildSessions.readLog(id, offset, length) },
        buildLogLength = { id -> buildSessions.logLength(id) },
        isBuildActive = { id -> port.history.firstOrNull { it.id == id }?.phase == PortPhase.RUNNING },
        emptyBuildLogMessage = { id ->
            val session = port.history.firstOrNull { it.id == id }
            locale.str(when {
                session == null || !session.logAvailable -> R.string.builds_log_unavailable
                session.phase == PortPhase.RUNNING -> R.string.builds_log_waiting
                session.legacy -> R.string.builds_log_legacy
                else -> R.string.builds_log_empty
            })
        },
        titleBuildLog = { id -> port.history.firstOrNull { it.id == id }?.let {
            "${File(it.game).name} · ${java.text.SimpleDateFormat("yyyy-MM-dd HH:mm", java.util.Locale.getDefault()).format(java.util.Date(it.startedAt))}"
        } ?: locale.str(R.string.common_log) },
    )

    /**
     * 导入固件包：解压到 App 的私有目录（后台线程，340 MB 的包要十几秒）。
     * 解的是模拟器那种固件包（zip 根下一堆 .nca）；解压后的那份就是移植器要的「系统内容文件夹」。
     */
    private fun importFirmware(zip: File) {
        if (!zip.isFile) { toast(locale.str(R.string.fw_import_failed, zip.name)); return }
        lifecycleScope.launch {
            val n = withContext(Dispatchers.IO) {
                runCatching { firmwareRepo.unpack(zip) }
            }
            n.onSuccess { toast(locale.str(R.string.fw_imported, zip.name, it)); envCheck.refresh() }   // the chip's colour follows it at once
                .onFailure { toast(locale.str(R.string.fw_import_failed, zip.name)) }
        }
    }

    override fun onResume() {
        super.onResume()
        onboarding.storageOk = storageGranted()   // the permission may have been given in the background
        onboarding.batteryOk = batteryUnrestricted()   // so may the battery exemption
        // 回到前台：顶栏胶囊的两个事实重读一遍（玩家可能在后台改了文件）
        envCheck.refresh()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (intent.hasExtra(EXTRA_TAB) || intent.hasExtra(EXTRA_BUILD_ID)) {
            launchTab = intent.getIntExtra(EXTRA_TAB, 1)
            launchBuildId = intent.getStringExtra(EXTRA_BUILD_ID).orEmpty()
        }
    }

    /** The phone's language changed (configChanges holds it): the app's words are read again */
    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        locale.configurationChanged()
    }

    // ---- the storage (the activity's own privilege) ----------------------------------------------------------

    /** The shared storage: the game files and the ports the tool makes. From Android 11 the old permissions are
     * not enough: the app needs the all-files access, which the user gives once */
    private fun storageGranted(): Boolean {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager()
        return checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
    }

    fun askStorage() {
        try {
            if (Build.VERSION.SDK_INT >= 30) {
                startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
            } else {
                requestPermissions(arrayOf(android.Manifest.permission.READ_EXTERNAL_STORAGE,
                                          android.Manifest.permission.WRITE_EXTERNAL_STORAGE), 1)
            }
        } catch (e: Exception) {
            try {
                startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
            } catch (e2: Exception) {
                toast(locale.str(R.string.main_no_permission_settings))
            }
        }
    }

    // ---- the battery (the system's own privilege) ------------------------------------------------------------

    /**
     * Whether the app is exempt from the power saving. The unpack and a port are long background jobs:
     * the power saving stops them halfway without it.
     *
     * Some ROMs never report the exemption (the setting lives in their own power manager), and a phone
     * that cannot answer must not look broken: a failure counts as exempt. The check is a hint, not a
     * gate, so nothing depends on the answer being right.
     */
    private fun batteryUnrestricted(): Boolean = try {
        val pm = getSystemService(android.os.PowerManager::class.java)
        pm == null || pm.isIgnoringBatteryOptimizations(packageName)
    } catch (e: Exception) {
        true
    }

    /**
     * Android's own prompt: the player allows the app to run in the background there. The list of the
     * exempt apps is the fallback for the ROMs that have no prompt of their own.
     */
    fun askBattery() {
        try {
            startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                                 Uri.parse("package:$packageName")))
        } catch (e: Exception) {
            try {
                startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
            } catch (e2: Exception) {
                toast(locale.str(R.string.main_no_battery_settings))
            }
        }
    }

    // ---- after the port (the activity's own privilege) -----------------------------------------------------------

    fun versionName(): String = AppVersion.of(this).ifEmpty { "?" }

    fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()

    /** Saves a completed standalone APK through Android's document picker without copying it into another cache. */
    private var pendingApkExport: File? = null
    private val apkExportLauncher = registerForActivityResult(
        ActivityResultContracts.CreateDocument("application/vnd.android.package-archive")
    ) { uri ->
        val source = pendingApkExport
        pendingApkExport = null
        if (uri == null || source == null) return@registerForActivityResult
        lifecycleScope.launch {
            val error = withContext(Dispatchers.IO) {
                runCatching {
                    val output = contentResolver.openOutputStream(uri, "w")
                        ?: throw java.io.IOException("the selected destination could not be opened")
                    output.use { out -> source.inputStream().use { input -> input.copyTo(out, 1 shl 20) } }
                }.exceptionOrNull()
            }
            if (error == null) toast(locale.str(R.string.builds_apk_saved, source.name))
            else {
                AppLog.line("[RED] the game APK could not be saved: ${error.message}")
                toast(locale.str(R.string.builds_apk_save_failed, error.message ?: source.name))
            }
        }
    }

    private fun exportApk(apk: File) {
        if (!apk.isFile) { toast(locale.str(R.string.lib_artifact_missing_state)); return }
        val dataFolder = File(apk.parentFile, "${apk.nameWithoutExtension}-data")
        if (dataFolder.isDirectory) {
            // Android cannot install ZIP64 APKs. A large game uses the original
            // per-game APK + companion data export instead: never suggest the
            // copied APK alone contains the full port.
            android.app.AlertDialog.Builder(this)
                .setTitle(locale.str(R.string.builds_apk_companion_title))
                .setMessage(locale.str(R.string.builds_apk_companion_message, dataFolder.absolutePath))
                .setPositiveButton(locale.str(R.string.common_ok)) { _, _ ->
                    pendingApkExport = apk
                    apkExportLauncher.launch(apk.name)
                }
                .setNegativeButton(locale.str(R.string.common_cancel), null)
                .show()
        } else {
            pendingApkExport = apk
            apkExportLauncher.launch(apk.name)
        }
    }

    /**
     * The log bundle's folder in the cache: the FileProvider serves it (res/xml/file_paths.xml), and the
     * bundles older than a week are deleted when a new one is packed.
     */
    private fun unfinishedStageLogs(record: PortState.Record): List<Pair<String, File>> {
        if (record.phase != PortPhase.RUNNING && record.phase != PortPhase.INTERRUPTED) return emptyList()
        return buildWorkspaces.stageLogs(record.game).map { (name, file) -> "${record.id}/$name" to file }
    }

    private fun variableDiagnostics(history: List<PortState.Record>): List<Pair<String, String>> =
        history.mapNotNull { record ->
            record.config?.let { "build-variables/${record.id}.txt" to it.diagnosticVariables() }
        }

    private fun sensitiveVariables(history: List<PortState.Record>): List<String> =
        history.mapNotNull { it.config?.sharedFont?.takeIf(String::isNotBlank) }

    private fun bundleDir() = File(cacheDir, "shared-logs")

    /** Sends the log bundle (a zip) out; nothing when there was nothing to pack */
    private fun shareBundle(zip: File?, titleRes: Int, emptyRes: Int) {
        if (zip == null) {
            toast(locale.str(emptyRes))
            return
        }
        try {
            val uri = FileProvider.getUriForFile(this, "$packageName.fileprovider", zip)
            val send = Intent(Intent.ACTION_SEND).setType("application/zip").putExtra(Intent.EXTRA_STREAM, uri)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            startActivity(Intent.createChooser(send, locale.str(titleRes)))
        } catch (e: Exception) {
            AppLog.line("[RED] the log bundle could not be shared: ${e.message}")
            toast(locale.str(R.string.main_log_bundle_failed))
        }
    }

    // ---- Activity-owned state holders injected by Hilt ------------------------------------------------------------

}
