package com.nx.android.feature.builds

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.compose.runtime.snapshotFlow
import com.nx.android.appstate.LibraryState
import com.nx.android.appstate.PortState
import dagger.hilt.android.lifecycle.HiltViewModel
import java.io.File
import com.nx.android.domain.porting.PortPhase
import com.nx.android.domain.BuildOptions
import com.nx.android.domain.GameConfig
import com.nx.android.domain.forNewBuilderDraft
import com.nx.android.domain.GameInspector
import com.nx.android.data.PrefsStore
import com.nx.android.data.porting.BuildSessionRepository
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

data class BuildsUiState(
    val target: String? = null,
    val phase: PortPhase = PortPhase.IDLE,
    val progress: Float = 0f,
    val gameFile: String = "",
    val output: String = "",
    val startedAt: Long = 0L,
    val stageOf: Int = 0,
    val stages: Int = 0,
    val stageName: String = "",
    val stageStep: String = "",
    val log: List<String> = emptyList(),
    val history: List<PortState.Record> = emptyList(),
    val running: Boolean = false,
)

/** What the page has to say out loud by itself. */
enum class BuildsNotice {
    /** The game has no profile for Android: the experimental port is ticked and the player is told why. */
    EXPERIMENTAL_AUTO,
}

@HiltViewModel
class BuildsViewModel @Inject constructor(
    private val library: LibraryState,
    val port: PortState,
    private val sessions: BuildSessionRepository,
    inspector: GameInspector,
    private val prefs: PrefsStore,
) : ViewModel() {
    var selectedBuildId by mutableStateOf("")
        private set
    private var targetFile by mutableStateOf<File?>(null)
    private var inspectedKeys: String? = null
    var draft by mutableStateOf(GameConfig())
        private set
    var draftRevision by mutableStateOf(0)
        private set
    val hasDraft get() = targetFile != null && !port.running
    val experimentalRequired get() = gameInfo.info?.experimental == true

    /** What the selected game file holds (the porter's own reading of it) */
    val gameInfo = GameInfoReader(inspector, viewModelScope)

    /** A message for the screen: the counter gives every notice its own, so the same one can come twice. */
    var noticeSeq by mutableStateOf(0)
        private set
    var notice by mutableStateOf<BuildsNotice?>(null)
        private set

    // Eagerly：这一页不在眼前时也跟着一份状态（切回来时第一帧就是选中的那个游戏，不是空的那一页）
    val uiState: StateFlow<BuildsUiState> = snapshotFlow {
        BuildsUiState(
            target = targetFile?.absolutePath,
            phase = port.phase,
            progress = port.progress,
            gameFile = port.gameFile,
            output = port.outPath,
            startedAt = port.startedAt,
            stageOf = port.stageOf,
            stages = port.stages,
            stageName = port.stageName,
            stageStep = port.stageStep,
            log = port.log,
            history = port.history,
            running = port.running,
        )
    }.stateIn(viewModelScope, SharingStarted.Eagerly, BuildsUiState())

    init {
        // 每读完一次游戏信息就照着它做一次决定（读到的答案属于当前选中的那个文件）
        viewModelScope.launch {
            snapshotFlow { gameInfo.reads }.collect { applyProfile() }
        }
        val initialBuildId = port.buildId
        viewModelScope.launch {
            snapshotFlow { port.buildId }.collect { id ->
                if (id.isNotBlank() && id != initialBuildId && targetFile?.absolutePath == port.gameFile) {
                    setTarget(null)
                }
            }
        }
        viewModelScope.launch {
            snapshotFlow { prefs.keys }.collect { keys ->
                val file = targetFile
                if (file != null && keys != inspectedKeys && !port.running) inspect(file, keys)
            }
        }
    }

    fun setTarget(file: File?, previous: GameConfig? = null) {
        if (file != null && port.running) return
        selectedBuildId = ""
        targetFile = file
        draftRevision++
        draft = (previous ?: file?.let(library::configOf) ?: GameConfig()).forNewBuilderDraft()
        if (file == null) {
            inspectedKeys = null
            gameInfo.clear()
        } else {
            inspect(file, prefs.keys)
        }
        applyProfile()
    }

    private fun inspect(file: File, keys: String) {
        inspectedKeys = keys
        gameInfo.read(file, keys)
    }

    fun changeOptions(touch: Boolean = draft.touch, fps60: Boolean = draft.fps60) {
        val file = targetFile ?: return
        if (!hasDraft) return
        draft = draft.copy(touch = touch, fps60 = fps60)
        // Preserve existing regular build preferences; the advanced variables live only in this draft.
        library.setConfig(file, library.configOf(file).copy(touch = touch, fps60 = fps60))
    }

    fun changeVariables(expectedRevision: Int = draftRevision, change: (GameConfig) -> GameConfig) {
        if (!hasDraft || expectedRevision != draftRevision) return
        val updated = change(draft)
        draft = if (experimentalRequired) updated.copy(experimental = true) else updated
    }

    fun resetVariables() = changeVariables { it.forNewBuilderDraft() }

    fun openBuild(id: String) { selectedBuildId = id }
    fun closeBuild() { selectedBuildId = "" }
    fun logTail(id: String): List<String> = if (id == port.buildId) port.log else sessions.tail(id)
    fun fullLog(id: String): String = runCatching {
        val file = sessions.logFile(id) ?: return ""
        file.readText(Charsets.UTF_8)
    }.getOrDefault("")

    fun clearLog() = port.clearLog()

    /** The screen has said the notice out loud. */
    fun clearNotice() { notice = null }

    private fun applyProfile() {
        if (!hasDraft) return
        val updated = BuildOptions.withExperimental(draft, gameInfo.info) ?: return
        draft = updated
        notice = BuildsNotice.EXPERIMENTAL_AUTO
        noticeSeq++
    }
}
