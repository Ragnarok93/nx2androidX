package com.nx.android.feature.debug

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.nx.android.R
import com.nx.android.app.AppActions
import com.nx.android.appstate.PortState
import com.nx.android.appstate.RuntimeUpdateState
import com.nx.android.core.ui.components.BuilderListRow
import com.nx.android.core.ui.components.BuilderPage
import com.nx.android.core.ui.components.Section
import com.nx.android.data.AppLog
import com.nx.android.feature.logs.LogOverlay
import java.io.File

@Composable
fun DebugScreen(
    port: PortState, runtime: RuntimeUpdateState, actions: AppActions, onLog: (String) -> Unit,
    onAppLog: () -> Unit, onEnvironmentCheck: () -> Unit, onVariables: () -> Unit,
) {
    var showUpgradeLog by rememberSaveable { mutableStateOf(false) }
    var confirmClear by rememberSaveable { mutableStateOf(false) }
    BuilderPage {
        Section(stringResource(R.string.common_log)) {
            val latest = port.history.firstOrNull()
            if (latest != null) {
                BuilderListRow(stringResource(R.string.builder_build_log), File(latest.game).name) { onLog(latest.id) }
                HorizontalDivider()
            }
            BuilderListRow(stringResource(R.string.builder_app_log), onClick = onAppLog)
            if (runtime.apkUpgrade && runtime.apkLog.isNotEmpty()) {
                HorizontalDivider()
                BuilderListRow(stringResource(R.string.builder_upgrade_log)) { showUpgradeLog = true }
            }
        }
        Section(stringResource(R.string.builder_variables)) {
            BuilderListRow(stringResource(R.string.builder_variables_open),
                stringResource(R.string.builder_variables_help), onClick = onVariables)
        }
        Section(stringResource(R.string.settings_diagnostic_log)) {
            BuilderListRow(stringResource(R.string.builder_capture),
                stringResource(if (AppLog.captureActive) R.string.builder_capture_running else R.string.builder_capture_idle))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                TextButton(onClick = {
                    if (AppLog.captureActive) AppLog.stopCapture() else AppLog.startCapture()
                }) { Text(stringResource(if (AppLog.captureActive) R.string.builder_capture_stop else R.string.builder_capture_start)) }
                TextButton(onClick = actions.onShareLog) { Text(stringResource(R.string.builder_export_diagnostics)) }
                TextButton(onClick = { confirmClear = true }) { Text(stringResource(R.string.builds_log_clear)) }
            }
            HorizontalDivider()
            BuilderListRow(stringResource(R.string.builder_troubleshooting), onClick = onEnvironmentCheck)
        }
    }
    if (showUpgradeLog) {
        BackHandler { showUpgradeLog = false }
        LogOverlay(onClose = { showUpgradeLog = false },
            text = runtime.apkLog.joinToString("\n"), title = stringResource(R.string.builder_upgrade_log))
    }
    if (confirmClear) AlertDialog(
        onDismissRequest = { confirmClear = false },
        title = { Text(stringResource(R.string.builds_log_clear)) },
        confirmButton = {
            TextButton(onClick = { confirmClear = false; actions.onClearLog() }) {
                Text(stringResource(R.string.common_delete))
            }
        },
        dismissButton = { TextButton(onClick = { confirmClear = false }) { Text(stringResource(R.string.common_cancel)) } },
    )
}
