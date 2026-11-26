package com.nx.android.feature.upgrade

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.nx.android.R
import com.nx.android.core.ui.components.BuilderPage
import com.nx.android.core.ui.components.Section
import com.nx.android.appstate.RuntimeUpdatePhase
import com.nx.android.appstate.RuntimeUpdateState
import com.nx.android.feature.filepicker.PickerMode
import com.nx.android.feature.filepicker.PickerRequest
import java.io.File

/** The selection survives switching tabs and Activity recreation. */
class UpgradeSelection(source: String = "", companion: String = "", output: String = "",
                       installed: String = "", key: String = "") {
    var source by mutableStateOf(source)
    var companion by mutableStateOf(companion)
    var output by mutableStateOf(output)
    var installed by mutableStateOf(installed)
    var key by mutableStateOf(key)
    val outputPath get() = output.ifBlank { RuntimeUpdateState.upgradedPath(source) }

    fun select(file: File) {
        source = file.absolutePath
        output = ""
        installed = ""
        key = ""
        val adjacent = File(file.parentFile, file.nameWithoutExtension + "-data")
        companion = adjacent.takeIf { it.isDirectory }?.absolutePath.orEmpty()
    }

    fun select(game: InstalledGameApk) {
        source = game.source.absolutePath
        companion = game.companion?.absolutePath.orEmpty()
        output = game.output.absolutePath
        installed = game.packageName
        key = ""
    }

    companion object {
        val Saver = listSaver<UpgradeSelection, String>(
            save = { listOf(it.source, it.companion, it.output, it.installed, it.key) },
            restore = { UpgradeSelection(it[0], it[1], it.getOrElse(2) { "" },
                it.getOrElse(3) { "" }, it.getOrElse(4) { "" }) }
        )
    }
}

@Composable
fun UpgradeScreen(selection: UpgradeSelection, runtime: RuntimeUpdateState,
                  busy: Boolean, ready: Boolean, onRequest: (PickerRequest) -> Unit,
                  onUpgrade: (File, File?, File?, File?) -> Unit, onExport: (File) -> Unit,
                  onInstall: (File) -> Unit) {
    val context = LocalContext.current
    var refresh by remember { mutableIntStateOf(0) }
    val installedGames by produceState<List<InstalledGameApk>?>(null, refresh) {
        value = null
        value = withContext(Dispatchers.IO) { runCatching { InstalledGameApks.discover(context) }.getOrDefault(emptyList()) }
    }
    val pickTitle = stringResource(R.string.upgrade_pick_apk)
    val dataTitle = stringResource(R.string.upgrade_pick_data)
    val active = runtime.apkUpgrade && runtime.running
    LaunchedEffect(runtime.apkUpgrade, runtime.pack) {
        if (runtime.apkUpgrade && selection.source.isBlank()) {
            selection.source = runtime.pack
            selection.companion = runtime.apkData
        }
    }
    LaunchedEffect(installedGames, selection.source) {
        if (selection.output.isBlank()) {
            installedGames.orEmpty().firstOrNull { it.source.absolutePath == selection.source }?.let { game ->
                selection.output = game.output.absolutePath
                selection.installed = game.packageName
                if (selection.companion.isBlank()) selection.companion = game.companion?.absolutePath.orEmpty()
            }
        }
    }
    BuilderPage {
        Text(stringResource(R.string.upgrade_description), style = MaterialTheme.typography.bodyLarge)
        Section(stringResource(R.string.upgrade_installed_games)) {
            OutlinedButton(enabled = !busy, onClick = { refresh++ }) {
                Text(stringResource(R.string.upgrade_refresh_installed))
            }
            if (installedGames == null) LinearProgressIndicator(Modifier.fillMaxWidth())
            else if (installedGames.orEmpty().isEmpty()) Text(stringResource(R.string.upgrade_no_installed))
            installedGames.orEmpty().forEach { game ->
                OutlinedButton(enabled = !busy, modifier = Modifier.fillMaxWidth(), onClick = {
                    if (!runtime.running && runtime.apkUpgrade) runtime.dismiss()
                    selection.select(game)
                }) {
                    Column(Modifier.fillMaxWidth()) {
                        Text(game.name, fontWeight = FontWeight.Medium)
                        Text(game.version + " · " + game.packageName, style = MaterialTheme.typography.bodySmall)
                        if (game.needsCompanion && game.companion == null)
                            Text(stringResource(R.string.upgrade_companion_required), style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
        Section(stringResource(R.string.builder_existing_port)) {
            Text(selection.source.ifBlank { stringResource(R.string.upgrade_no_apk) })
            OutlinedButton(enabled = !busy, onClick = {
                onRequest(PickerRequest(PickerMode.FILE, setOf("apk"),
                    selection.source.takeIf { it.isNotBlank() }?.let { File(it) }, pickTitle,
                    onPicked = { file ->
                        if (!runtime.running && runtime.apkUpgrade) runtime.dismiss()
                        selection.select(file)
                    }))
            }) { Text(pickTitle) }
            HorizontalDivider()
            Text(selection.companion.ifBlank { stringResource(R.string.upgrade_data_optional) })
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(enabled = !busy && selection.source.isNotBlank(), onClick = {
                    onRequest(PickerRequest(PickerMode.DIR, emptySet(),
                        selection.companion.takeIf { it.isNotBlank() }?.let { File(it) }
                            ?: File(selection.source).parentFile, dataTitle,
                        onPicked = { selection.companion = it.absolutePath }))
                }) { Text(dataTitle) }
                if (selection.companion.isNotBlank()) {
                    TextButton(enabled = !busy, onClick = { selection.companion = "" }) {
                        Text(stringResource(R.string.upgrade_clear_data))
                    }
                }
            }
            HorizontalDivider()
            Text(stringResource(R.string.upgrade_key_explanation), style = MaterialTheme.typography.bodySmall)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(enabled = !busy && selection.source.isNotBlank(), onClick = {
                    onRequest(PickerRequest(PickerMode.FILE, setOf("json"), null,
                        context.getString(R.string.upgrade_restore_key),
                        onPicked = { selection.key = it.absolutePath }))
                }) { Text(stringResource(R.string.upgrade_restore_key)) }
                if (selection.key.isNotBlank()) TextButton(enabled = !busy, onClick = { selection.key = "" }) {
                    Text(stringResource(R.string.upgrade_clear_key))
                }
            }
            if (selection.key.isNotBlank()) Text(File(selection.key).name, style = MaterialTheme.typography.bodySmall)
            if (selection.source.isNotBlank()) Text(stringResource(R.string.upgrade_output, selection.outputPath))
            if (!ready) Text(stringResource(R.string.upgrade_prepare_environment), color = MaterialTheme.colorScheme.error)
            Button(
                enabled = !busy && ready && selection.source.isNotBlank(),
                onClick = {
                    onUpgrade(File(selection.source), selection.companion.takeIf { it.isNotBlank() }?.let { File(it) },
                        selection.output.takeIf { it.isNotBlank() }?.let { File(it) },
                        selection.key.takeIf { it.isNotBlank() }?.let { File(it) })
                },
                modifier = Modifier.fillMaxWidth()
            ) { Text(stringResource(R.string.upgrade_start)) }
        }
        if (runtime.apkUpgrade && runtime.phase != RuntimeUpdatePhase.IDLE) {
            Surface(modifier = Modifier.fillMaxWidth(), shape = MaterialTheme.shapes.large,
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f)) {
                Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(runtime.name, fontWeight = FontWeight.Bold)
                    when (runtime.phase) {
                        RuntimeUpdatePhase.RUNNING -> {
                            Text(stringResource(R.string.upgrade_running))
                            LinearProgressIndicator(Modifier.fillMaxWidth())
                        }
                        RuntimeUpdatePhase.SUCCEEDED -> {
                            Text(stringResource(R.string.upgrade_success))
                            Text(runtime.apkOutput, style = MaterialTheme.typography.bodySmall)
                            if (selection.installed.isNotBlank() && runtime.pack == selection.source &&
                                runtime.apkOutput == selection.output) {
                                Button(onClick = { onInstall(File(runtime.apkOutput)) }, modifier = Modifier.fillMaxWidth()) {
                                    Text(stringResource(R.string.upgrade_install))
                                }
                            }
                            OutlinedButton(onClick = { onExport(File(runtime.apkOutput)) }) {
                                Text(stringResource(R.string.upgrade_export))
                            }
                        }
                        RuntimeUpdatePhase.FAILED -> Text(
                            runtime.reason.ifBlank { stringResource(if (runtime.interrupted) R.string.upgrade_interrupted else R.string.upgrade_failed) },
                            color = MaterialTheme.colorScheme.error)
                        else -> Unit
                    }
                    if (runtime.apkLog.isNotEmpty()) {
                        Text(stringResource(R.string.common_log), fontWeight = FontWeight.Bold)
                        runtime.apkLog.takeLast(if (active) 8 else 12).forEach {
                            Text(it, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }
    }
}
