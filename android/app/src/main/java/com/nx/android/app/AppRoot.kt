package com.nx.android.app

import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.outlined.BugReport
import androidx.compose.material.icons.outlined.Build
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Upgrade
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.runtime.key
import com.nx.android.appstate.RuntimeUpdateState
import com.nx.android.core.ui.theme.BuilderTheme
import com.nx.android.core.ui.theme.BuilderThemeMode
import com.nx.android.feature.builds.RecompilationVariablesScreen
import com.nx.android.feature.debug.DebugScreen
import com.nx.android.feature.upgrade.UpgradeScreen
import com.nx.android.feature.upgrade.UpgradeSelection
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.hilt.lifecycle.viewmodel.compose.hiltViewModel
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.text.style.TextOverflow
import com.nx.android.core.ui.components.LocalBuilderBottomPadding
import com.nx.android.core.ui.components.BuilderTopAppBar
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.nx.android.R
import com.nx.android.appstate.PortState
import com.nx.android.appstate.OnboardingState
import com.nx.android.data.AppLocale
import com.nx.android.data.GameUpdateInputs
import com.nx.android.data.UpgradeTarget
import com.nx.android.data.PrefsStore
import com.nx.android.data.StorageVolumes
import com.nx.android.domain.GameConfig
import com.nx.android.feature.builds.BuildsRoute
import com.nx.android.feature.builds.BuildsViewModel
import com.nx.android.core.ui.components.CapsuleButton
import com.nx.android.core.ui.ProvideAppLocale
import com.nx.android.feature.filepicker.FilePickerScreen
import com.nx.android.feature.logs.LogOverlay
import com.nx.android.feature.filepicker.PickerMode
import com.nx.android.feature.filepicker.PickerRequest
import com.nx.android.core.ui.components.QuietButton
import com.nx.android.feature.environment.EnvChecklistDialog
import com.nx.android.feature.storage.StorageViewModel
import com.nx.android.feature.onboarding.OnboardingViewModel
import com.nx.android.feature.environment.EnvironmentCheckViewModel
import com.nx.android.domain.drivers.DriverRepository
import com.nx.android.domain.environment.EnvironmentSetup
import com.nx.android.feature.onboarding.OnboardingScreen
import com.nx.android.feature.drivers.DriverRoute
import com.nx.android.feature.settings.SettingsScreen
import com.nx.android.feature.settings.SettingsSection
import com.nx.android.feature.updates.openRepo
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

private enum class Tab(@StringRes val label: Int, val icon: ImageVector) {
    BUILD(R.string.builder_build, Icons.Outlined.Build),
    UPGRADE(R.string.app_tab_upgrade, Icons.Outlined.Upgrade),
    SETTINGS(R.string.app_tab_settings, Icons.Outlined.Settings),
    DEBUG(R.string.builder_debug, Icons.Outlined.BugReport),
}

/** The screens' way back to the activity's own privileges: what they may ask for, nothing else */
data class AppActions(
    val onAskStorage: () -> Unit,
    val onAskBattery: () -> Unit,
    val storageOk: () -> Boolean,
    val onSetKeys: (String) -> Unit,
    val onStartPort: (file: File, cfg: GameConfig) -> Unit,
    val onCancelPort: () -> Unit,
    val onExportApk: (apk: File) -> Unit,
    val onToast: (String) -> Unit,
    val version: () -> String,
    val keys: () -> String,
    val onShareLog: () -> Unit,
    val onClearLog: () -> Unit,
    val readLog: () -> String,
    val onShareBuildLog: (String) -> Unit,
    val readBuildLog: (String, Long, Int) -> String,
    val buildLogLength: (String) -> Long,
    val isBuildActive: (String) -> Boolean,
    val emptyBuildLogMessage: (String) -> String,
    val titleBuildLog: (String) -> String,
    val onUpgradeApk: (UpgradeTarget, File?, GameUpdateInputs?, File?) -> Unit = { _, _, _, _ -> },
    val onCancelUpgrade: () -> Unit = {},
    val onInstallUpgrade: () -> Unit = {},
    val onExportUpgrade: () -> Unit = {},
    // the firmware: one for the machine (imported from a zip into the app's own folder, optional)
    val firmware: () -> String = { "" },
    val onImportFirmware: (File) -> Unit = {},
    val onClearFirmware: () -> Unit = {},
    // the file provider: the app's own folders are visible to the file managers (on by default)
    val fileProvider: () -> Boolean = { true },
    val onSetFileProvider: (Boolean) -> Unit = {},
)

@Composable
fun AppRoot(
    prefs: PrefsStore,
    locale: AppLocale,
    port: PortState,
    onboarding: com.nx.android.appstate.OnboardingState,
    drivers: DriverRepository,
    environmentSetup: EnvironmentSetup,
    actions: AppActions,
    startTab: Int,
    runtime: com.nx.android.appstate.RuntimeUpdateState,
    launchBuildId: String,
) {
    var theme by remember { mutableStateOf(BuilderThemeMode.fromPreference(prefs.str(PrefsStore.BUILDER_THEME))) }
    BuilderTheme(theme) {
        // 界面语言：整棵树读同一份资源，玩家在设置里换语言时这里跟着换
        ProvideAppLocale(locale) {
            var pickerReq by remember { mutableStateOf<PickerRequest?>(null) }
            var driversOpen by remember { mutableStateOf(false) }
            var logOpen by remember { mutableStateOf(false) }
            var buildLogId by remember { mutableStateOf("") }
            var envCheckOpen by remember { mutableStateOf(false) }
            var ossNotice by rememberSaveable { mutableStateOf(false) }
            val ctx = androidx.compose.ui.platform.LocalContext.current
            val onboardingViewModel: OnboardingViewModel = hiltViewModel()
            val onboardingUiState by onboardingViewModel.uiState.collectAsStateWithLifecycle()
            val environmentCheckViewModel: EnvironmentCheckViewModel = hiltViewModel()
            // 开源声明：引导走完的那一刻弹一次，老用户（早已完成引导）升级后第一次启动也弹；记在 prefs 里，只此一次
            LaunchedEffect(onboardingUiState.active) {
                if (!onboardingUiState.active
                    && prefs.bool(PrefsStore.OB_COMPLETED)
                    && !prefs.bool(PrefsStore.OSS_NOTICE_SHOWN)) ossNotice = true
            }
            Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).windowInsetsPadding(WindowInsets.safeDrawing)) {
                if (onboardingUiState.active) {
                    OnboardingScreen(onboardingViewModel, actions) { pickerReq = it }
                } else {
                    MainScreen(prefs, locale, port, onboarding, drivers, environmentSetup, actions, startTab, runtime, launchBuildId,
                        theme = theme,
                        onTheme = { mode ->
                            prefs.put(PrefsStore.BUILDER_THEME, mode.name)
                            theme = mode
                        },
                        onRequest = { pickerReq = it },
                        onDrivers = { driversOpen = true },
                        onLog = { id -> buildLogId = id },
                        onDiagnosticLog = { logOpen = true },
                        onEnvCheck = { envCheckOpen = true })
                }
                pickerReq?.let { req ->
                    var storageRevision by remember(req) { mutableIntStateOf(0) }
                    LifecycleEventEffect(Lifecycle.Event.ON_RESUME) { storageRevision++ }
                    DisposableEffect(ctx, req) {
                        val receiver = object : BroadcastReceiver() {
                            override fun onReceive(context: Context, intent: Intent) { storageRevision++ }
                        }
                        val filter = IntentFilter().apply {
                            addAction(Intent.ACTION_MEDIA_MOUNTED)
                            addAction(Intent.ACTION_MEDIA_UNMOUNTED)
                            addAction(Intent.ACTION_MEDIA_EJECT)
                            addAction(Intent.ACTION_MEDIA_REMOVED)
                            addAction(Intent.ACTION_MEDIA_BAD_REMOVAL)
                            addDataScheme("file")
                        }
                        ContextCompat.registerReceiver(ctx, receiver, filter, ContextCompat.RECEIVER_EXPORTED)
                        onDispose { ctx.unregisterReceiver(receiver) }
                    }
                    val storageOk = remember(req, storageRevision) { actions.storageOk() }
                    Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.45f))) {
                        if (!storageOk) {
                            BackHandler { pickerReq = null }
                            // 内置文件管理器读不了没有全文件访问的存储：给授权入口
                            Column(Modifier.fillMaxSize().padding(24.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
                                    verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                    Text(stringResource(R.string.app_storage_permission_needed),
                                        style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                                    Text(stringResource(R.string.app_storage_permission_picker),
                                        style = MaterialTheme.typography.bodyMedium)
                                }
                                FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp, Alignment.End),
                                    modifier = Modifier.fillMaxWidth()) {
                                    QuietButton(stringResource(R.string.common_cancel), onClick = { pickerReq = null })
                                    CapsuleButton(stringResource(R.string.app_grant_permission), onClick = actions.onAskStorage)
                                }
                            }
                        } else {
                            val volumes = remember(ctx, locale.mode, storageRevision) { StorageVolumes.list(ctx, locale) }
                            FilePickerScreen(
                                req = req,
                                volumes = volumes,
                                storageRevision = storageRevision,
                                onPicked = { f ->
                                    pickerReq = null
                                    req.onPicked(f)
                                },
                                onCancel = { pickerReq = null },
                            )
                        }
                    }
                }
                // 驱动页：盖住整屏，在页面之外
                if (driversOpen) {
                    DriverRoute(onClose = { driversOpen = false })
                }
                // 环境检查清单：顶栏胶囊点开的，盖住整屏
                if (envCheckOpen) {
                    BackHandler { envCheckOpen = false }
                    EnvChecklistDialog(
                        viewModel = environmentCheckViewModel,
                        onboarding = onboardingViewModel,
                        portRunning = port.running,
                        onPrepareEnv = { onboardingViewModel.unpack() },
                        onPickKeys = {
                            envCheckOpen = false
                            pickerReq = PickerRequest(PickerMode.FILE, emptySet(), null,
                                locale.str(R.string.onb_choose_prod_keys),
                                onPicked = { f -> actions.onSetKeys(f.path) })
                        },
                        firmware = actions.firmware(),
                        onImportFirmware = {
                            envCheckOpen = false
                            pickerReq = PickerRequest(PickerMode.FILE, setOf("zip"), null,
                                locale.str(R.string.fw_pick),
                                onPicked = { f -> actions.onImportFirmware(f) })
                        },
                        onRunOnboarding = {
                            // the checklist first: it is drawn over the setup and would hide it
                            envCheckOpen = false
                            onboarding.open()
                        },
                        onDismiss = { envCheckOpen = false },
                    )
                }
                // 日志浮层：同样盖住整屏
                if (logOpen) {
                    BackHandler { logOpen = false }
                    LogOverlay(onClose = { logOpen = false }, text = remember { actions.readLog() })
                }
                if (buildLogId.isNotEmpty()) {
                    BackHandler { buildLogId = "" }
                    data class InitialBuildLog(val text: String, val offset: Long, val length: Long)
                    val page by produceState(initialValue = InitialBuildLog("", 0, 0), buildLogId) {
                        val length = withContext(Dispatchers.IO) { actions.buildLogLength(buildLogId) }
                        val offset = (length - 64 * 1024).coerceAtLeast(0)
                        val text = withContext(Dispatchers.IO) { actions.readBuildLog(buildLogId, offset, 64 * 1024) }
                        value = InitialBuildLog(text, offset, length)
                    }
                    LogOverlay(
                        onClose = { buildLogId = "" }, text = page.text,
                        title = actions.titleBuildLog(buildLogId), buildId = buildLogId,
                        initialOffset = page.offset, initialLength = page.length,
                        active = actions.isBuildActive(buildLogId),
                        emptyText = actions.emptyBuildLogMessage(buildLogId),
                        readRange = { offset, length -> actions.readBuildLog(buildLogId, offset, length) },
                        latestLength = { actions.buildLogLength(buildLogId) },
                    )
                }
                // 开源声明：盖在最后面，玩家先看到它
                if (ossNotice) {
                    OpenSourceNoticeDialog(
                        onClose = {
                            ossNotice = false
                            prefs.put(PrefsStore.OSS_NOTICE_SHOWN, true)
                        },
                        onRepo = {
                            openRepo(ctx)
                            ossNotice = false
                            prefs.put(PrefsStore.OSS_NOTICE_SHOWN, true)
                        },
                    )
                }
            }
        }
    }
}

/**
 * 开源声明：这个软件是免费开源的，禁止盗卖；花钱买到的请联系卖家退款。
 *
 * 它只出现一次：记在 prefs 里，关掉就不再出现。
 */
@Composable
private fun OpenSourceNoticeDialog(onClose: () -> Unit, onRepo: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        shape = MaterialTheme.shapes.large,
        containerColor = MaterialTheme.colorScheme.surface,
        title = { Text(stringResource(R.string.oss_notice_title), fontWeight = FontWeight.Bold) },
        text = { Text(stringResource(R.string.oss_notice_body), Modifier.verticalScroll(rememberScrollState())) },
        confirmButton = {
            TextButton(onClick = onClose) { Text(stringResource(R.string.oss_notice_ok), color = MaterialTheme.colorScheme.primary) }
        },
        dismissButton = {
            TextButton(onClick = onRepo) { Text(stringResource(R.string.oss_notice_repo)) }
        },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun MainScreen(
    prefs: PrefsStore,
    locale: AppLocale,
    port: PortState,
    onboarding: OnboardingState,
    drivers: DriverRepository,
    environmentSetup: EnvironmentSetup,
    actions: AppActions,
    startTab: Int,
    runtime: RuntimeUpdateState,
    launchBuildId: String,
    theme: BuilderThemeMode,
    onTheme: (BuilderThemeMode) -> Unit,
    onRequest: (PickerRequest) -> Unit,
    onDrivers: () -> Unit,
    onLog: (String) -> Unit,
    onDiagnosticLog: () -> Unit,
    onEnvCheck: () -> Unit,
) {
    var destination by rememberSaveable { mutableStateOf(Tab.BUILD) }
    var settingsSection by rememberSaveable { mutableStateOf<SettingsSection?>(null) }
    var variablesOpen by rememberSaveable { mutableStateOf(false) }
    var variablesFromBuild by rememberSaveable { mutableStateOf(false) }
    val builds: BuildsViewModel = hiltViewModel()
    val storage: StorageViewModel = hiltViewModel()
    val installedDrivers by drivers.drivers.collectAsStateWithLifecycle()
    val upgrade = rememberSaveable(saver = UpgradeSelection.Saver) { UpgradeSelection() }
    val environment: EnvironmentCheckViewModel = hiltViewModel()
    val check by environment.uiState.collectAsStateWithLifecycle()
    val ready = if (check.known) check.envReady else environmentSetup.ready
    LaunchedEffect(startTab, launchBuildId) {
        if (launchBuildId.isNotBlank()) {
            destination = Tab.BUILD
            builds.openBuild(launchBuildId)
        } else if (startTab == 2) {
            // Keep notification and intent values issued by older versions.
            destination = Tab.UPGRADE
        }
    }
    fun select(tab: Tab) {
        destination = tab
        variablesOpen = false
        settingsSection = null
    }
    fun closeDetail() {
        if (destination == Tab.BUILD) {
            builds.closeBuild()
            return
        }
        if (variablesOpen && variablesFromBuild) select(Tab.BUILD) else select(destination)
    }
    val buildDetail = destination == Tab.BUILD && builds.selectedBuildId.isNotEmpty()
    val detail = buildDetail || (destination == Tab.DEBUG && variablesOpen) ||
        (destination == Tab.SETTINGS && settingsSection != null)
    BackHandler(enabled = detail) { closeDetail() }
    val title = when {
        buildDetail -> R.string.builds_result
        destination == Tab.DEBUG && variablesOpen -> R.string.builder_variables
        destination == Tab.SETTINGS -> settingsSection?.title ?: destination.label
        else -> destination.label
    }

    BoxWithConstraints(Modifier.fillMaxSize()) {
        val wide = maxWidth >= 600.dp
        val railOnSide = wide || maxHeight < 430.dp
        Row(Modifier.fillMaxSize()) {
            if (railOnSide) NavigationRail(
                modifier = Modifier.verticalScroll(rememberScrollState()).padding(vertical = 8.dp),
                containerColor = MaterialTheme.colorScheme.surface,
                windowInsets = WindowInsets(0, 0, 0, 0),
            ) {
                Tab.entries.forEach { tab ->
                    NavigationRailItem(
                        selected = destination == tab,
                        onClick = { select(tab) },
                        icon = { Icon(tab.icon, contentDescription = null) },
                        label = { Text(stringResource(tab.label)) },
                    )
                }
            }
            Column(Modifier.weight(1f)) {
                BuilderTopAppBar(stringResource(title), onBack = if (detail) ::closeDetail else null)
                if (destination != Tab.BUILD && port.running) TextButton(onClick = { select(Tab.BUILD) }) {
                    Text(stringResource(R.string.builds_porting))
                }
                var pillHeight by remember { mutableIntStateOf(0) }
                val bottomPadding = if (railOnSide) 0.dp else with(LocalDensity.current) {
                    if (pillHeight > 0) pillHeight.toDp() else 92.dp
                }
                Box(Modifier.weight(1f).fillMaxWidth()) {
                    key(destination, title) {
                        CompositionLocalProvider(LocalBuilderBottomPadding provides bottomPadding) {
                            when (destination) {
                                Tab.BUILD -> BuildsRoute(builds, prefs.keys, ready, actions,
                                    onRequest, onLog, busy = runtime.running,
                                    onVariables = {
                                        select(Tab.DEBUG)
                                        variablesFromBuild = true
                                        variablesOpen = true
                                    },
                                    onSetup = { select(Tab.SETTINGS); settingsSection = SettingsSection.ENVIRONMENT })
                                Tab.UPGRADE -> UpgradeScreen(upgrade, runtime, port.running || runtime.running,
                                    ready, port.history, onRequest, actions)
                                Tab.SETTINGS -> SettingsScreen(onboarding, installedDrivers, drivers, storage,
                                    actions, onRequest, onDrivers, settingsSection, { settingsSection = it }, theme, onTheme)
                                Tab.DEBUG -> if (variablesOpen) {
                                    RecompilationVariablesScreen(builds, onRequest,
                                        onInvalidFont = { actions.onToast(locale.str(R.string.builder_font_invalid)) },
                                        onReview = { select(Tab.BUILD) })
                                } else {
                                    DebugScreen(port, runtime, actions, onLog, onDiagnosticLog, onEnvCheck,
                                        onVariables = { variablesFromBuild = false; variablesOpen = true })
                                }
                            }
                        }
                    }
                    if (!railOnSide) {
                        val background = MaterialTheme.colorScheme.background
                        Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth()) {
                            Box(Modifier.matchParentSize().background(Brush.verticalGradient(
                                0f to Color.Transparent,
                                0.65f to background.copy(alpha = 0.26f),
                                1f to background,
                            )))
                            NavigationBar(
                                modifier = Modifier.onSizeChanged { pillHeight = it.height }
                                    .padding(horizontal = 12.dp, vertical = 6.dp)
                                    .clip(MaterialTheme.shapes.large),
                                containerColor = MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.78f),
                                tonalElevation = 0.dp,
                                windowInsets = WindowInsets(0, 0, 0, 0),
                            ) {
                                Tab.entries.forEach { tab ->
                                    NavigationBarItem(
                                        selected = destination == tab,
                                        onClick = { select(tab) },
                                        icon = { Icon(tab.icon, contentDescription = null) },
                                        label = { Text(stringResource(tab.label), maxLines = 1,
                                            overflow = TextOverflow.Ellipsis) },
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
