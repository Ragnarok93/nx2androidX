package com.nx.android.feature.upgrade

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.nx.android.R
import com.nx.android.app.AppActions
import com.nx.android.appstate.PortState
import com.nx.android.appstate.RuntimeUpdatePhase
import com.nx.android.appstate.RuntimeUpdateState
import com.nx.android.core.ui.components.BuilderListRow
import com.nx.android.core.ui.components.BuilderPage
import com.nx.android.core.ui.components.Section
import com.nx.android.data.GameUpdateInputs
import com.nx.android.data.UpgradeApks
import com.nx.android.data.UpgradeTarget
import com.nx.android.domain.GAME_FILE_EXTENSIONS
import com.nx.android.feature.filepicker.PickerMode
import com.nx.android.feature.filepicker.PickerRequest
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/** The selection survives switching tabs and activity recreation. */
class UpgradeSelection(
    source: String = "",
    companion: String = "",
    gameUpdate: Boolean = false,
    patch: String = "",
    base: String = "",
    key: String = "",
) {
    var revision by mutableIntStateOf(0); private set
    var source by mutableStateOf(source)
    var companion by mutableStateOf(companion)
    var gameUpdate by mutableStateOf(gameUpdate)
    var patch by mutableStateOf(patch)
    // An empty choice uses the base that produced this exact port, if it is still available.
    var base by mutableStateOf(base)
    var signingKey by mutableStateOf(key)

    fun select(file: File) {
        revision++
        source = file.absolutePath
        companion = ""
        gameUpdate = false
        patch = ""
        base = ""
        signingKey = ""
    }

    companion object {
        val Saver = listSaver<UpgradeSelection, String>(
            save = { listOf(it.source, it.companion, it.gameUpdate.toString(), it.patch, it.base, it.signingKey) },
            restore = { UpgradeSelection(it[0], it[1], it.getOrNull(2).toBoolean(), it.getOrElse(3) { "" },
                it.getOrElse(4) { "" }, it.getOrElse(5) { "" }) },
        )
    }
}

private fun String.pickedFile() = takeIf(String::isNotBlank)?.let(::File)

@Composable
fun UpgradeScreen(selection: UpgradeSelection, runtime: RuntimeUpdateState, busy: Boolean,
                  ready: Boolean, history: List<PortState.Record>, onRequest: (PickerRequest) -> Unit,
                  actions: AppActions) {
    val context = LocalContext.current.applicationContext
    val apks = remember(context) { UpgradeApks(context) }
    var installedOpen by remember { mutableStateOf(false) }
    LaunchedEffect(runtime.interrupted) {
        if (runtime.interrupted && runtime.apkUpgrade && selection.source.isBlank())
            selection.source = runtime.pack
    }
    val selectedPath = selection.source
    val inspection by key(selectedPath, selection.revision) {
        produceState<Result<UpgradeTarget>?>(null) {
            if (selectedPath.isNotEmpty()) value = withContext(Dispatchers.IO) {
                runCatching { apks.inspect(File(selectedPath)) }
            }
        }
    }
    val target = inspection?.getOrNull()
    val discoveredBase by key(target, history, selection.gameUpdate) {
        produceState<File?>(null) {
            if (target != null && selection.gameUpdate) {
                value = withContext(Dispatchers.IO) {
                    history.asSequence().filter { it.out.endsWith(".apk", ignoreCase = true) }
                        .mapNotNull { record ->
                            val base = File(record.game)
                            if (!base.canRead()) return@mapNotNull null
                            val output = File(record.out)
                            if (output == target.apk || runCatching {
                                    apks.archiveIdentity(output) == target.identity
                                }.getOrDefault(false)) base else null
                        }.firstOrNull()
                }
            }
        }
    }
    val companion = selection.companion.pickedFile() ?: target?.companion
    val base = selection.base.pickedFile() ?: discoveredBase
    val patch = selection.patch.pickedFile()
    val pickApk = stringResource(R.string.upgrade_pick_apk)
    val pickData = stringResource(R.string.upgrade_pick_data)
    val pickPatch = stringResource(R.string.upgrade_pick_patch)
    val pickBase = stringResource(R.string.upgrade_pick_base)
    val pickKey = stringResource(R.string.upgrade_restore_key)
    val signingKey = selection.signingKey.pickedFile()
    val active = runtime.apkUpgrade && runtime.running

    fun select(file: File) {
        if (runtime.apkUpgrade) runtime.dismiss()
        selection.select(file)
    }

    BuilderPage {
        Section(stringResource(R.string.builder_existing_port)) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(enabled = !busy, onClick = {
                    onRequest(PickerRequest(PickerMode.FILE, setOf("apk"),
                        selection.source.pickedFile(), pickApk, onPicked = ::select))
                }) { Text(pickApk) }
                OutlinedButton(enabled = !busy, onClick = { installedOpen = true }) {
                    Text(stringResource(R.string.upgrade_pick_installed))
                }
            }
            when {
                selectedPath.isBlank() -> Text(stringResource(R.string.upgrade_no_apk))
                inspection == null -> Text(stringResource(R.string.upgrade_reading))
                target != null -> {
                    Text(target.label, style = MaterialTheme.typography.titleMedium)
                    Text(listOf(target.identity.packageName, target.version)
                        .filter(String::isNotBlank).joinToString(" · "), style = MaterialTheme.typography.bodySmall)
                }
                else -> Text(inspection?.exceptionOrNull()?.message.orEmpty(), color = MaterialTheme.colorScheme.error)
            }
            if (target != null && !target.embedded) {
                HorizontalDivider()
                Text(companion?.absolutePath ?: stringResource(R.string.upgrade_companion_required),
                    style = MaterialTheme.typography.bodyMedium)
                OutlinedButton(enabled = !busy, onClick = {
                    onRequest(PickerRequest(PickerMode.DIR, emptySet(), companion ?: target.apk.parentFile,
                        pickData, onPicked = { selection.companion = it.absolutePath }))
                }) { Text(pickData) }
            }
        }
        if (target != null) {
            val canStart = !busy && ready && (target.embedded || companion != null)
            Section(stringResource(R.string.upgrade_action)) {
                Text(stringResource(R.string.upgrade_key_explanation), style = MaterialTheme.typography.bodySmall)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(enabled = !busy, onClick = {
                        onRequest(PickerRequest(PickerMode.FILE, setOf("json"), signingKey, pickKey,
                            onPicked = { selection.signingKey = it.absolutePath }))
                    }) { Text(pickKey) }
                    if (signingKey != null) TextButton(enabled = !busy, onClick = { selection.signingKey = "" }) {
                        Text(stringResource(R.string.upgrade_clear_key))
                    }
                }
                signingKey?.let { Text(it.name, style = MaterialTheme.typography.bodySmall) }
                if (!ready) Text(stringResource(R.string.upgrade_prepare_environment), color = MaterialTheme.colorScheme.error)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(enabled = canStart, onClick = {
                        selection.gameUpdate = false
                        actions.onUpgradeApk(target, companion, null, signingKey)
                    }) { Text(stringResource(R.string.upgrade_runtime)) }
                    OutlinedButton(enabled = !busy, onClick = { selection.gameUpdate = !selection.gameUpdate }) {
                        Text(stringResource(R.string.upgrade_game))
                    }
                }
                if (selection.gameUpdate) {
                    patch?.let { Text(it.name, style = MaterialTheme.typography.bodyMedium) }
                    base?.let { Text(stringResource(R.string.upgrade_using_base, it.name),
                        style = MaterialTheme.typography.bodyMedium) }
                    if (patch != null && base == null) Text(stringResource(R.string.upgrade_base_required))
                    val keysReady = actions.keys().isNotBlank()
                    if (!keysReady) Text(stringResource(R.string.builds_need_keys), color = MaterialTheme.colorScheme.error)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(enabled = !busy, onClick = {
                            onRequest(PickerRequest(PickerMode.FILE, GAME_FILE_EXTENSIONS, patch ?: base, pickPatch,
                                onPicked = { selection.patch = it.absolutePath }))
                        }) { Text(pickPatch) }
                        OutlinedButton(enabled = !busy, onClick = {
                            onRequest(PickerRequest(PickerMode.FILE, GAME_FILE_EXTENSIONS, base ?: patch, pickBase,
                                onPicked = { selection.base = it.absolutePath }))
                        }) { Text(pickBase) }
                    }
                    Button(enabled = canStart && keysReady && patch != null && base != null, onClick = {
                        if (patch != null && base != null) {
                            actions.onUpgradeApk(target, companion, GameUpdateInputs(patch, base), signingKey)
                        }
                    }) { Text(stringResource(R.string.upgrade_build_game)) }
                }
            }
        }
        if (runtime.apkUpgrade && runtime.phase != RuntimeUpdatePhase.IDLE) {
            Section(runtime.name) {
                when (runtime.phase) {
                    RuntimeUpdatePhase.RUNNING -> {
                        Text(stringResource(if (runtime.apkRequest?.game != null)
                            R.string.upgrade_game_running else R.string.upgrade_running))
                        LinearProgressIndicator(Modifier.fillMaxWidth())
                        OutlinedButton(onClick = actions.onCancelUpgrade) { Text(stringResource(R.string.common_cancel)) }
                    }
                    RuntimeUpdatePhase.SUCCEEDED -> {
                        Text(stringResource(R.string.upgrade_success))
                        Text(runtime.apkOutput, style = MaterialTheme.typography.bodySmall)
                        if (runtime.apkData.isNotBlank()) {
                            Text(stringResource(R.string.upgrade_companion_location, runtime.apkData),
                                style = MaterialTheme.typography.bodySmall)
                        }
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(onClick = actions.onInstallUpgrade) { Text(stringResource(R.string.upgrade_install)) }
                            OutlinedButton(onClick = actions.onExportUpgrade) { Text(stringResource(R.string.upgrade_export)) }
                        }
                    }
                    RuntimeUpdatePhase.FAILED -> Text(runtime.reason.ifBlank {
                        stringResource(if (runtime.interrupted) R.string.upgrade_interrupted else R.string.upgrade_failed)
                    }, color = MaterialTheme.colorScheme.error)
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
    if (installedOpen) {
        InstalledGamesDialog(apks, onDismiss = { installedOpen = false }, onSelect = {
            installedOpen = false
            select(it)
        })
    }
}

@Composable
private fun InstalledGamesDialog(apks: UpgradeApks, onDismiss: () -> Unit, onSelect: (File) -> Unit) {
    val games by produceState<List<Pair<String, File>>?>(null, apks) {
        value = withContext(Dispatchers.IO) { apks.installedGames() }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.upgrade_pick_installed)) },
        text = {
            val list = games
            when {
                list == null -> LinearProgressIndicator(Modifier.fillMaxWidth())
                list.isEmpty() -> Text(stringResource(R.string.upgrade_no_installed))
                else -> LazyColumn(Modifier.fillMaxWidth().heightIn(max = 420.dp)) {
                    items(list, key = { it.second.absolutePath }) { (label, apk) ->
                        BuilderListRow(label) { onSelect(apk) }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.common_cancel)) } },
    )
}
