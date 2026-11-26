package com.nx.android.feature.builds

import com.nx.android.domain.porting.PortPhase

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nx.android.R
import com.nx.android.appstate.PortState
import com.nx.android.domain.GameFile
import com.nx.android.domain.GameIdentity
import com.nx.android.domain.GameInfo
import com.nx.android.domain.GameProfileStatus
import com.nx.android.domain.GAME_FILE_EXTENSIONS
import com.nx.android.domain.GameStatus
import com.nx.android.app.AppActions
import com.nx.android.core.ui.components.BuilderListRow
import com.nx.android.core.ui.components.BuilderPage
import com.nx.android.core.ui.components.SwitchRow
import com.nx.android.core.ui.LocalAppLocale
import com.nx.android.core.ui.components.CapsuleButton
import com.nx.android.core.ui.components.Hint
import com.nx.android.feature.filepicker.PickerMode
import com.nx.android.feature.filepicker.PickerRequest
import com.nx.android.core.ui.components.QuietButton
import com.nx.android.core.ui.components.Section
import com.nx.android.core.ui.components.StatusDot
import com.nx.android.core.ui.elapsedSecs
import com.nx.android.core.ui.theme.NxPalette
import java.io.File

/** Select and review source files, then follow the build through APK export. */
@Composable
fun BuildsRoute(viewModel: BuildsViewModel, keys: String, envReady: Boolean,
                actions: AppActions, onRequest: (PickerRequest) -> Unit, onLog: (String) -> Unit,
                busy: Boolean, onVariables: () -> Unit, onSetup: () -> Unit) {
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val locale = LocalAppLocale.current
    // 页面自己要说的话（没有档案的游戏被勾上了实验性移植）：屏幕把它念出来
    LaunchedEffect(viewModel.noticeSeq) {
        when (viewModel.notice) {
            BuildsNotice.EXPERIMENTAL_AUTO -> {
                viewModel.clearNotice()
                actions.onToast(locale.str(R.string.builds_experimental_auto))
            }
            null -> Unit
        }
    }
    BuildsScreen(viewModel, uiState, keys, envReady, actions, onRequest, onLog, busy, onVariables, onSetup)
}

@Composable
private fun BuildsScreen(
    viewModel: BuildsViewModel, uiState: BuildsUiState, keys: String, envReady: Boolean,
    actions: AppActions, onRequest: (PickerRequest) -> Unit, onLog: (String) -> Unit,
    busy: Boolean, onVariables: () -> Unit, onSetup: () -> Unit,
) {
    val locale = LocalAppLocale.current
    val selected = uiState.history.firstOrNull { it.id == viewModel.selectedBuildId }
    val current = uiState.history.firstOrNull { it.id == viewModel.port.buildId }
    fun chooseSource() = onRequest(PickerRequest(PickerMode.FILE, GAME_FILE_EXTENSIONS, File("/storage"),
        locale.str(R.string.builds_choose_game_file), onPicked = { viewModel.setTarget(it) }))
    BuilderPage {
        when {
            selected != null -> {
                TaskDetail(selected, viewModel, actions, onLog, active = selected.id == current?.id && uiState.running)
            }
            uiState.running && current != null -> TaskDetail(current, viewModel, actions, onLog, active = true)
            uiState.running -> ProgressCard(uiState, actions)
            viewModel.hasDraft && uiState.target != null -> TargetCard(
                viewModel, keys, envReady, File(uiState.target), actions, busy, onVariables, onSetup, ::chooseSource)
            else -> {
                Section(stringResource(R.string.builds_start_title)) {
                    Text(stringResource(R.string.builds_start_hint), style = MaterialTheme.typography.bodyMedium)
                    CapsuleButton(stringResource(R.string.builds_choose_game_file), ::chooseSource, enabled = !busy)
                }
                if (current != null) TaskDetail(current, viewModel, actions, onLog, active = false)
                if (uiState.history.isNotEmpty()) History(uiState, viewModel)
            }
        }
    }
}

@Composable
private fun TaskDetail(record: PortState.Record, viewModel: BuildsViewModel,
                       actions: AppActions, onLog: (String) -> Unit, active: Boolean) {
    val lines = viewModel.logTail(record.id)
    // 进行中的这一轮：秒表从它的起点算（record.seconds 是结束时才写下的时长，进行中永远是 0）
    val secs = if (active) elapsedSecs(record.startedAt) else record.seconds
    Section(stringResource(R.string.builds_result)) {
        Text(File(record.game).name, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
        Text(stringResource(R.string.builds_history_seconds, (secs / 60).toString(), (secs % 60).toString()))
        PhaseChip(if (active) PortPhase.RUNNING else record.phase)
        if (active) {
            val port = viewModel.port
            if (port.stages > 0) Text(stringResource(R.string.port_stage_of, port.stageOf, port.stages, port.stageName))
            if (port.stageStep.isNotBlank()) Text(port.stageStep, style = MaterialTheme.typography.bodyMedium)
            if (port.stages > 0) LinearProgressIndicator(progress = { port.progress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth())
            else LinearProgressIndicator(Modifier.fillMaxWidth())
            QuietButton(stringResource(R.string.builds_cancel_port), actions.onCancelPort)
        }
        record.config?.let { Hint(buildVariablesSummary(it)) }
        if (record.legacy) Text(stringResource(R.string.builds_log_legacy), style = MaterialTheme.typography.bodySmall)
        else if (!record.logAvailable) Text(stringResource(R.string.builds_log_unavailable), style = MaterialTheme.typography.bodySmall,
            color = NxPalette.Danger)
        LogTail(lines)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            TextButton(onClick = { onLog(record.id) }) { Text(stringResource(R.string.builds_log_view)) }
            if (record.logAvailable) TextButton(onClick = { actions.onShareBuildLog(record.id) }) {
                Text(stringResource(R.string.builds_log_share))
            }
            if (!active && !viewModel.port.running && File(record.game).isFile) {
                TextButton(onClick = { viewModel.setTarget(File(record.game), record.config) }) {
                    Text(stringResource(R.string.builds_build_again))
                }
            }
        }
        if (record.phase == PortPhase.DONE && record.out.endsWith(".apk", ignoreCase = true)) {
            val apk = File(record.out)
            if (apk.isFile) {
                Text(apk.absolutePath, style = MaterialTheme.typography.bodyMedium)
                CapsuleButton(stringResource(R.string.builds_export_apk), { actions.onExportApk(apk) })
            } else Text(stringResource(R.string.lib_artifact_missing_state), color = MaterialTheme.colorScheme.error)
        }
    }
}

// ---- 目标卡（未运行时）：游戏与参数，开始按钮 ------------------------------------------------------------------

@Composable
private fun TargetCard(
    viewModel: BuildsViewModel, keys: String, envReady: Boolean, f: File, actions: AppActions,
    busy: Boolean, onVariables: () -> Unit, onSetup: () -> Unit, onChoose: () -> Unit,
) {
    val cfg = viewModel.draft
    val info = viewModel.gameInfo.info
    val storageOk = actions.storageOk()
    val canStart = f.isFile && f.canRead() && envReady && storageOk && !busy &&
        keys.isNotBlank() && !viewModel.gameInfo.reading &&
        (cfg.sharedFont.isBlank() || isBuildFont(File(cfg.sharedFont)))

    Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Section(stringResource(R.string.builder_sources)) {
            TargetIdentity(f, info)
            Text(f.absolutePath, style = MaterialTheme.typography.bodyMedium)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = onChoose) { Text(stringResource(R.string.builds_change)) }
                TextButton(onClick = { viewModel.setTarget(null) }) { Text(stringResource(R.string.builds_clear)) }
            }
        }

        // 识别到的文件：移植器认出了什么、从哪认出的、这一轮打哪个版本，以及文件该怎么放
        if (info != null || viewModel.gameInfo.reading) {
            Section(stringResource(R.string.builds_files_title)) {
                if (info == null) {
                    Hint(stringResource(R.string.builds_files_reading))
                } else {
                    if (info.base.isNotEmpty()) {
                        FileLine(stringResource(R.string.builds_files_base), info.base,
                            onClick = { actions.onToast(info.base) })
                    }
                    FileList(stringResource(R.string.builds_files_update), info.updates, actions)
                    FileList(stringResource(R.string.builds_files_dlc), info.dlc, actions)
                    if (info.packVersion.isNotEmpty()) {
                        Text(stringResource(R.string.builds_info_pack, info.packVersion),
                            style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onSurface, fontSize = 11.sp)
                    }
                    Hint(stringResource(R.string.builds_files_hint), size = 11.sp)
                }
            }
        }

        Section(stringResource(R.string.builder_review)) {
            SwitchRow(stringResource(R.string.builds_touch_game), cfg.touch, onChange = { viewModel.changeOptions(touch = it) })
            HorizontalDivider()
            SwitchRow(stringResource(R.string.builds_60fps), cfg.fps60, onChange = { viewModel.changeOptions(fps60 = it) })
            HorizontalDivider()
            BuilderListRow(stringResource(R.string.builder_variables), buildVariablesSummary(cfg), onClick = onVariables)
            Text(stringResource(R.string.builder_output_location), style = MaterialTheme.typography.bodyMedium)
            if (!canStart) {
                Text(when {
                    busy -> stringResource(R.string.runtime_busy)
                    !envReady -> stringResource(R.string.builds_need_env)
                    !storageOk -> stringResource(R.string.builds_need_storage)
                    keys.isBlank() -> stringResource(R.string.builds_need_keys)
                    !f.isFile -> stringResource(R.string.builds_source_missing)
                    !f.canRead() -> stringResource(R.string.builds_needs_storage)
                    viewModel.gameInfo.reading -> stringResource(R.string.builds_files_reading)
                    else -> stringResource(R.string.builder_font_invalid)
                }, style = MaterialTheme.typography.bodyMedium)
                if (!envReady || !storageOk || keys.isBlank()) {
                    TextButton(onClick = onSetup) { Text(stringResource(R.string.env_section)) }
                }
            }
            CapsuleButton(stringResource(R.string.builder_compile), { actions.onStartPort(f, cfg) },
                enabled = canStart, modifier = Modifier.fillMaxWidth())
        }
    }
}

/** 认到的文件的一行：分类标签 + 文件名（+ 它在哪个子文件夹里） */
@Composable
private fun FileLine(label: String, name: String, folder: String = "", onClick: (() -> Unit)? = null) {
    Row(
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp),
        modifier = Modifier.fillMaxWidth().let { if (onClick == null) it else it.clickable { onClick() } },
    ) {
        Text(label, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontSize = 11.sp, modifier = Modifier.width(32.dp))
        Text(name, style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
            color = MaterialTheme.colorScheme.onSurface, fontSize = 11.sp, maxLines = 1,
            overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
        if (folder.isNotEmpty()) {
            Text(stringResource(R.string.builds_files_folder, folder),
                style = MaterialTheme.typography.bodySmall, color = NxPalette.Brand, fontSize = 11.sp)
        }
    }
}

/** 一类文件：本体旁边的更新或 DLC。多了只列前几个，其余的说明还有多少（点一下全列出来） */
@Composable
private fun FileList(label: String, files: List<GameFile>, actions: AppActions) {
    if (files.isEmpty()) return
    val shown = files.take(2)
    for (f in shown) {
        FileLine(label, f.name, f.folder, onClick = { actions.onToast(f.detail()) })
    }
    if (files.size > shown.size) {
        Text(stringResource(R.string.builds_files_more, files.size - shown.size),
            style = MaterialTheme.typography.bodySmall, color = NxPalette.Brand, fontSize = 11.sp,
            modifier = Modifier
                .padding(start = 40.dp)
                .clickable { actions.onToast(files.joinToString("\n") { it.detail() }) })
    }
}

/** 一个文件说全了：名字、版本与它所在的子文件夹 */
private fun GameFile.detail(): String =
    listOf(name, version.takeIf { it.isNotEmpty() }?.let { "($it)" }, folder.takeIf { it.isNotEmpty() }?.let { "$it/" })
        .filterNotNull().joinToString("  ")

/**
 * 目标卡的身份块：这是哪个游戏（状态徽章 + 名字）、它的 title ID、这一轮会打包哪个版本，
 * 以及同一文件夹里有多少更新与 DLC。
 *
 * 名字与 title ID 各只在这里出现一次；源文件的路径另起一行。
 * 移植器的读数没回来之前不显示猜测出来的东西：徽章、计数与打包版本都等它。
 */
@Composable
private fun TargetIdentity(f: File, info: GameInfo?) {
    val name = info?.name?.takeIf { it.isNotEmpty() } ?: GameIdentity.displayName(f)
    val titleId = info?.titleId?.takeIf { it.isNotEmpty() } ?: GameIdentity.titleIdIn(f.name)
    Text(name, style = MaterialTheme.typography.titleMedium)
    if (titleId != null) Text(titleId, style = MaterialTheme.typography.bodySmall)
    if (info != null) ProfileChip(info.status)
}

/** 档案状态：一种状态一个颜色，与游戏库那张卡上的是同一套 */
@Composable
private fun ProfileChip(status: GameProfileStatus) {
    val (label, color) = when (status) {
        GameProfileStatus.PLAYABLE -> stringResource(R.string.builds_info_playable) to NxPalette.Ready
        GameProfileStatus.WIP -> stringResource(R.string.builds_info_wip) to NxPalette.Building
        GameProfileStatus.BOOTS -> stringResource(R.string.builds_info_boots) to NxPalette.Building
        GameProfileStatus.EXPERIMENTAL -> stringResource(R.string.builds_info_experimental) to NxPalette.Unbuilt
        GameProfileStatus.IOS_ONLY -> stringResource(R.string.builds_info_ios_only) to NxPalette.Unbuilt
        GameProfileStatus.UNSUPPORTED -> stringResource(R.string.builds_info_unsupported) to NxPalette.Brand
    }
    Surface(shape = RoundedCornerShape(50), color = color.copy(alpha = 0.18f)) {
        Row(Modifier.padding(horizontal = 10.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            StatusDot(color, 8.dp)
            Text(label, fontSize = 12.sp, color = color, fontWeight = FontWeight.Bold)
        }
    }
}

/** 路径省略中间：头一段与最后两段留着，中间用省略号（文件在哪，一眼看得出；全路径按一下就看全了） */
internal fun shortPath(path: String): String {
    val parts = path.split('/').filter { it.isNotEmpty() }
    if (parts.size <= 4) return path
    return "/" + parts[0] + "/…/" + parts.takeLast(2).joinToString("/")
}

// ---- 进度卡（运行时） -------------------------------------------------------------------------------------------

@Composable
private fun ProgressCard(port: BuildsUiState, actions: AppActions) {
    val secs = elapsedSecs(port.startedAt)
    Section(stringResource(R.string.builds_port_action)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            PhaseChip(port.phase)
            Text(
                File(port.gameFile).name,
                style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Bold,
                modifier = Modifier.weight(1f), maxLines = 2,
            )
            if (port.startedAt > 0) {
                Text(stringResource(R.string.builds_minutes_seconds, secs / 60, secs % 60),
                     style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        if (port.progress > 0f || port.stageName.isNotEmpty()) {
            LinearProgressIndicator(
                progress = { port.progress },
                modifier = Modifier.fillMaxWidth().height(6.dp),
                color = NxPalette.Building,
            )
            if (port.stageName.isNotEmpty()) {
                Text(
                    stringResource(R.string.port_stage_of, port.stageOf, port.stages, port.stageName) +
                        (if (port.stageStep.isEmpty()) "" else " · ${port.stageStep}"),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        if (port.phase == PortPhase.RUNNING) {
            QuietButton(stringResource(R.string.builds_cancel_port), { actions.onCancelPort() }, modifier = Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun LogTail(log: List<String>) {
    Surface(
        shape = RoundedCornerShape(12.dp),
        color = Color.Black.copy(alpha = 0.35f),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.fillMaxWidth().padding(10.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            val shown = log.takeLast(8)
            if (shown.isEmpty()) {
                Text(stringResource(R.string.builds_no_output), style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            } else {
                for (line in shown) {
                    Text(
                        line,
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                        color = if (line.startsWith("[RED]")) NxPalette.Danger else MaterialTheme.colorScheme.onSurface,
                        fontSize = 11.sp, maxLines = 2, overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }
}

/** One color per outcome, the same the library's cards and the settings wear */
@Composable
fun PhaseChip(p: PortPhase) {
    val (label, color) = when (p) {
        PortPhase.RUNNING -> stringResource(R.string.common_porting) to NxPalette.Building
        PortPhase.DONE -> stringResource(R.string.common_done) to NxPalette.Ready
        PortPhase.FAILED -> stringResource(R.string.common_failed) to NxPalette.Danger
        PortPhase.CANCELLED -> stringResource(R.string.common_cancelled) to NxPalette.Unbuilt
        PortPhase.INTERRUPTED -> stringResource(R.string.common_interrupted) to NxPalette.Danger
        PortPhase.IDLE -> stringResource(R.string.common_idle) to NxPalette.Unbuilt
    }
    Surface(shape = RoundedCornerShape(50), color = color.copy(alpha = 0.18f)) {
        Row(Modifier.padding(horizontal = 10.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            StatusDot(color, 8.dp)
            Text(label, fontSize = 12.sp, color = color, fontWeight = FontWeight.Bold)
        }
    }
}

private fun statusColor(s: GameStatus) = when (s) {
    GameStatus.NOT_PORTED -> NxPalette.Unbuilt
    GameStatus.PORTING -> NxPalette.Building
    GameStatus.DONE -> NxPalette.Ready
    GameStatus.NEEDS_BUILD -> NxPalette.Unbuilt
}

@Composable
private fun statusLabel(s: GameStatus): String = when (s) {
    GameStatus.NOT_PORTED -> stringResource(R.string.common_not_ported)
    GameStatus.PORTING -> stringResource(R.string.common_porting)
    GameStatus.DONE -> stringResource(R.string.common_done)
    GameStatus.NEEDS_BUILD -> stringResource(R.string.lib_needs_build_status)
}

/** Recent attempts: one row opens the result and log belonging to that attempt. */
@Composable
private fun History(port: BuildsUiState, viewModel: BuildsViewModel) {
    Section(stringResource(R.string.builds_history)) {
        for (r in port.history) {
            Column(Modifier.fillMaxWidth().clickable { viewModel.openBuild(r.id) }.padding(vertical = 8.dp)) {
                Text(File(r.game).name, style = MaterialTheme.typography.bodyMedium, maxLines = 1, fontWeight = FontWeight.Bold)
                Hint(stringResource(R.string.builds_history_seconds, r.seconds / 60, r.seconds % 60) + " · " + when (r.phase) {
                    PortPhase.DONE -> stringResource(R.string.common_complete)
                    PortPhase.FAILED -> stringResource(R.string.common_failed)
                    PortPhase.CANCELLED -> stringResource(R.string.common_cancelled)
                    PortPhase.INTERRUPTED -> stringResource(R.string.common_interrupted)
                    else -> ""
                })
            }
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
        }
        Hint(stringResource(R.string.builds_history_retention))
    }
}
