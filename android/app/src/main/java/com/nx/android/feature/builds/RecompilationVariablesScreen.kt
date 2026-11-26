package com.nx.android.feature.builds

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.ListItem
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.nx.android.R
import com.nx.android.core.ui.LocalAppLocale
import com.nx.android.core.ui.components.BuilderListRow
import com.nx.android.core.ui.components.BuilderPage
import com.nx.android.core.ui.components.CapsuleButton
import com.nx.android.core.ui.components.Section
import com.nx.android.core.ui.components.SwitchRow
import com.nx.android.domain.ALIAS_AUTO
import com.nx.android.domain.GameConfig
import com.nx.android.domain.buildAliasChoices
import com.nx.android.feature.filepicker.PickerMode
import com.nx.android.feature.filepicker.PickerRequest
import java.io.File

internal fun isBuildFont(file: File): Boolean =
    file.extension.equals("ttf", ignoreCase = true) && file.isFile && file.canRead()

@Composable
internal fun buildVariablesSummary(config: GameConfig): String {
    val names = listOfNotNull(
        stringResource(R.string.builds_experimental).takeIf { config.experimental },
        stringResource(R.string.builds_allow_damaged_media).takeIf { config.allowDamagedMedia },
        stringResource(R.string.builds_alias_short, config.aliasHeadroom).takeIf { config.aliasHeadroom != ALIAS_AUTO },
        stringResource(R.string.builds_shared_font_short, File(config.sharedFont).name).takeIf { config.sharedFont.isNotBlank() },
        stringResource(R.string.builds_mt_replay).takeIf { config.mtReplay },
    )
    return names.ifEmpty { listOf(stringResource(R.string.builder_no_overrides)) }.joinToString(" · ")
}

@Composable
fun RecompilationVariablesScreen(
    builds: BuildsViewModel,
    onRequest: (PickerRequest) -> Unit,
    onInvalidFont: () -> Unit,
    onReview: () -> Unit,
) {
    val hasDraft = builds.hasDraft
    val running = builds.port.running
    val cfg = if (running) builds.port.history.firstOrNull { it.id == builds.port.buildId }?.config else builds.draft
    var aliasOpen by rememberSaveable { mutableStateOf(false) }
    val locale = LocalAppLocale.current
    BuilderPage {
        Text(stringResource(when {
            running -> R.string.builder_variables_locked
            hasDraft -> R.string.builder_variables_help
            else -> R.string.builder_variables_choose_source
        }))
        if (cfg != null && (hasDraft || running)) {
            Section(stringResource(R.string.builder_variables)) {
                val experimentalNote = if (builds.experimentalRequired)
                    stringResource(R.string.builder_experimental_required)
                else stringResource(R.string.builder_experimental_purpose)
                VariableSwitch(R.string.builds_experimental, experimentalNote, cfg.experimental,
                    hasDraft && !builds.experimentalRequired, required = builds.experimentalRequired) { value ->
                    builds.changeVariables { it.copy(experimental = value) }
                }
                HorizontalDivider()
                VariableSwitch(R.string.builds_allow_damaged_media, stringResource(R.string.builds_allow_damaged_media_hint),
                    cfg.allowDamagedMedia, hasDraft) { value ->
                    builds.changeVariables { it.copy(allowDamagedMedia = value) }
                }
                HorizontalDivider()
                BuilderListRow(
                    stringResource(R.string.builds_alias_headroom),
                    stringResource(R.string.builds_alias_hint) + "\n" +
                        stringResource(R.string.builder_alias_values) + "\n" +
                        stringResource(R.string.builder_current_value, aliasLabel(cfg.aliasHeadroom)),
                    onClick = if (hasDraft) ({ aliasOpen = true }) else null,
                )
                HorizontalDivider()
                BuilderListRow(
                    stringResource(R.string.builds_shared_font),
                    stringResource(R.string.builds_shared_font_hint) + "\n" +
                        stringResource(R.string.builder_font_values) + "\n" +
                        stringResource(R.string.builder_current_value, cfg.sharedFont.takeIf(String::isNotBlank)
                            ?.let { File(it).name } ?: stringResource(R.string.builds_shared_font_default)),
                    onClick = if (hasDraft) ({
                        val revision = builds.draftRevision
                        onRequest(PickerRequest(PickerMode.FILE, setOf("ttf"), null,
                            locale.str(R.string.builds_shared_font_pick), onPicked = { file ->
                                if (isBuildFont(file)) builds.changeVariables(revision) { it.copy(sharedFont = file.absolutePath) }
                                else onInvalidFont()
                            }))
                    }) else null,
                )
                if (hasDraft && cfg.sharedFont.isNotBlank()) {
                    TextButton(onClick = { builds.changeVariables { it.copy(sharedFont = "") } }) {
                        Text(stringResource(R.string.builds_clear))
                    }
                }
                HorizontalDivider()
                VariableSwitch(R.string.builds_mt_replay, stringResource(R.string.builds_mt_replay_hint), cfg.mtReplay, hasDraft) { value ->
                    builds.changeVariables { it.copy(mtReplay = value) }
                }
            }
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            if (hasDraft) TextButton(onClick = builds::resetVariables) {
                Text(stringResource(R.string.builder_reset_variables))
            }
            CapsuleButton(stringResource(if (running) R.string.builder_view_build else R.string.builder_review), onReview)
        }
    }
    if (aliasOpen && hasDraft && cfg != null) AlertDialog(
        onDismissRequest = { aliasOpen = false },
        title = { Text(stringResource(R.string.builds_alias_headroom)) },
        text = {
            Column(Modifier.heightIn(max = 350.dp).verticalScroll(rememberScrollState())) {
                buildAliasChoices.forEach { value ->
                    ListItem(
                        headlineContent = { Text(aliasLabel(value)) },
                        leadingContent = { RadioButton(selected = value == cfg.aliasHeadroom, onClick = null) },
                        modifier = Modifier.fillMaxWidth().selectable(value == cfg.aliasHeadroom, role = Role.RadioButton) {
                            builds.changeVariables { it.copy(aliasHeadroom = value) }
                            aliasOpen = false
                        },
                    )
                }
            }
        },
        confirmButton = { TextButton(onClick = { aliasOpen = false }) { Text(stringResource(R.string.common_cancel)) } },
    )
}

@Composable
private fun aliasLabel(value: Int): String = if (value == ALIAS_AUTO) stringResource(R.string.builds_alias_auto)
    else stringResource(R.string.builder_alias_gb, value)

@Composable
private fun VariableSwitch(
    title: Int, purpose: String, checked: Boolean, editable: Boolean,
    required: Boolean = false, onChange: (Boolean) -> Unit,
) {
    val summary = purpose + "\n" + stringResource(if (required) R.string.builder_boolean_required else R.string.builder_boolean_values)
    if (editable) SwitchRow(stringResource(title), checked, summary, onChange)
    else BuilderListRow(stringResource(title), summary + "\n" + stringResource(
        R.string.builder_current_value, stringResource(if (checked) R.string.common_on else R.string.common_off)))
}
