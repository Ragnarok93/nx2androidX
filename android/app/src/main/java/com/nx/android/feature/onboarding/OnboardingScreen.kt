package com.nx.android.feature.onboarding

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nx.android.R
import com.nx.android.app.AppActions
import com.nx.android.core.ui.LocalAppLocale
import com.nx.android.core.ui.components.BuilderTopAppBar
import com.nx.android.core.ui.theme.BuilderSpacing
import com.nx.android.core.ui.theme.BuilderTheme
import com.nx.android.core.ui.theme.BuilderThemeMode
import com.nx.android.core.ui.components.CapsuleButton
import com.nx.android.domain.ObStep
import com.nx.android.domain.OnboardingLogic
import com.nx.android.feature.filepicker.PickerMode
import com.nx.android.feature.filepicker.PickerRequest

/**
 * Three setup steps and a completion screen. OnboardingLogic still owns the permission and readiness gates.
 *
 * 这一层是有状态的（它读 ViewModel，把动作接上）；画的那部分在 [OnboardingContent] 里，是无状态的，
 * 所以预览可以拿假状态单独看它。
 */
@Composable
fun OnboardingScreen(viewModel: OnboardingViewModel, actions: AppActions, onRequest: (PickerRequest) -> Unit) {
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val locale = LocalAppLocale.current

    // the back key walks the flow backwards, it does not leave the app
    BackHandler(enabled = uiState.step > ObStep.WELCOME) { viewModel.back() }

    OnboardingContent(
        uiState = uiState,
        actions = OnboardingActions(
            onNext = { viewModel.next() },
            onBack = { viewModel.back() },
            onOpenLibrary = { viewModel.openLibrary() },
            onAskStorage = { actions.onAskStorage() },
            onAskBattery = { actions.onAskBattery() },
            onUnpack = { viewModel.unpack() },
            // the picker's own title is a word of the app's language: it is read here, not in the step
            onChooseKeysFile = {
                onRequest(PickerRequest(PickerMode.FILE, setOf("keys"), uiState.keysStartDir,
                    locale.str(R.string.onb_choose_prod_keys),
                    onPicked = { f -> viewModel.chooseKeys(f.path) }))
            },
            onKeysSkippedChange = { if (it) viewModel.skipKeys() else viewModel.unskipKeys() },
            firmware = actions.firmware(),
            onClearFirmware = actions.onClearFirmware,
            onChooseFirmwareFile = {
                onRequest(PickerRequest(PickerMode.FILE, setOf("zip"), null,
                    locale.str(R.string.fw_pick),
                    onPicked = { f -> actions.onImportFirmware(f) }))
            },
        ),
    )
}

/** 页面上的动作：下一步、上一步、进入游戏库、要权限、要电池豁免、解包、选 keys、跳过 keys */
internal data class OnboardingActions(
    val onNext: () -> Unit,
    val onBack: () -> Unit,
    val onOpenLibrary: () -> Unit,
    val onAskStorage: () -> Unit,
    val onAskBattery: () -> Unit,
    val onUnpack: () -> Unit,
    val onChooseKeysFile: () -> Unit,
    val onKeysSkippedChange: (Boolean) -> Unit,
    // 固件（可选）：整机级的一份，导入 zip 解到 App 的私有目录
    val firmware: String = "",
    val onChooseFirmwareFile: () -> Unit = {},
    val onClearFirmware: () -> Unit = {},
)

/**
 * 引导页画的那一层：头（走到哪一步）、那一步、尾（上一步与这一步的主动作）。
 *
 * 无状态——只有 [uiState] 与动作；闸门（这一步能不能往下走）是纯函数算的。
 */
@Composable
internal fun OnboardingContent(uiState: OnboardingUiState, actions: OnboardingActions) {
    val can = OnboardingLogic.canAct(uiState.step, uiState.facts)

    Column(Modifier.fillMaxSize()) {
        BuilderTopAppBar(stepTitle(uiState.step),
            onBack = actions.onBack.takeIf { uiState.step > ObStep.WELCOME })
        Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.TopCenter) {
            Column(Modifier.widthIn(max = 640.dp).fillMaxSize()) {
                val stepCount = ObStep.entries.size - 1
                if (uiState.step != ObStep.DONE) {
                    LinearProgressIndicator(
                        progress = { (uiState.step.ordinal + 1f) / stepCount },
                        modifier = Modifier.fillMaxWidth()
                            .padding(horizontal = BuilderSpacing.screen, vertical = 8.dp).height(4.dp),
                        trackColor = MaterialTheme.colorScheme.surfaceVariant,
                    )
                }
                key(uiState.step) {
                    Column(
                        Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState())
                            .padding(BuilderSpacing.screen),
                        verticalArrangement = Arrangement.spacedBy(BuilderSpacing.section),
                    ) {
                        when (uiState.step) {
                            ObStep.WELCOME -> WelcomeStep(uiState.facts, actions.onAskStorage, actions.onAskBattery)
                            ObStep.ENV -> EnvStep(uiState, actions.onUnpack)
                            ObStep.KEYS -> KeysStep(uiState, actions.onChooseKeysFile, actions.onKeysSkippedChange,
                                actions.firmware, actions.onChooseFirmwareFile, actions.onClearFirmware)
                            ObStep.DONE -> DoneStep()
                        }
                    }
                }
                HorizontalDivider(Modifier.padding(horizontal = BuilderSpacing.screen),
                    color = MaterialTheme.colorScheme.outlineVariant)
                CapsuleButton(
                    text = primaryLabel(uiState.step),
                    onClick = { if (uiState.step == ObStep.DONE) actions.onOpenLibrary() else actions.onNext() },
                    enabled = can,
                    modifier = Modifier.fillMaxWidth().padding(BuilderSpacing.screen),
                )
            }
        }
    }
}

@Composable
private fun stepTitle(s: ObStep): String = when (s) {
    ObStep.WELCOME -> stringResource(R.string.onb_welcome, stringResource(R.string.app_name))
    ObStep.ENV -> stringResource(R.string.onb_prepare_env)
    ObStep.KEYS -> "prod.keys"
    ObStep.DONE -> stringResource(R.string.onb_all_ready)
}

@Composable
private fun primaryLabel(s: ObStep): String = when (s) {
    ObStep.WELCOME -> stringResource(R.string.common_start)
    ObStep.ENV, ObStep.KEYS -> stringResource(R.string.common_continue)
    ObStep.DONE -> stringResource(R.string.onb_enter_library)
}

/** 预览：第一步（权限与空间） */
@Preview(name = "Step, welcome", widthDp = 360, heightDp = 780, showBackground = true)
@Composable
private fun OnboardingWelcomePreview() {
    OnboardingPreview {
        OnboardingContent(previewState(ObStep.WELCOME), previewActions())
    }
}

/** 预览：第二步（环境还没准备好，主动作是灰的） */
@Preview(name = "Step, environment", widthDp = 360, heightDp = 780, showBackground = true)
@Composable
private fun OnboardingEnvPreview() {
    OnboardingPreview {
        OnboardingContent(previewState(ObStep.ENV), previewActions())
    }
}

/** 预览：第三步（keys） */
@Preview(name = "Step, keys", widthDp = 360, heightDp = 780, showBackground = true)
@Composable
private fun OnboardingKeysPreview() {
    OnboardingPreview {
        OnboardingContent(previewState(ObStep.KEYS), previewActions())
    }
}

/** 预览：最后一步（进入游戏库） */
@Preview(name = "Step, done", widthDp = 360, heightDp = 780, showBackground = true)
@Composable
private fun OnboardingDonePreview() {
    OnboardingPreview {
        OnboardingContent(previewState(ObStep.DONE), previewActions())
    }
}

@Composable
internal fun OnboardingPreview(content: @Composable () -> Unit) {
    BuilderTheme(BuilderThemeMode.SYSTEM) {
        Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) { content() }
    }
}
