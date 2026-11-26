package com.nx.android.feature.builds

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.nx.android.domain.GameInfo
import com.nx.android.domain.GameInspector
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * The builds page's reading of what the selected game file holds.
 *
 * One read per file: the answer is kept, so coming back to the same file does not start the guest again. A read
 * that arrives after the player picked another file is dropped — that answer belongs to the file it was asked
 * about.
 */
class GameInfoReader(
    private val inspector: GameInspector,
    private val scope: CoroutineScope,
) {

    /** What the selected file holds (null until the read comes back, and when it could not be read) */
    var info by mutableStateOf<GameInfo?>(null)
        private set

    /** A read is running */
    var reading by mutableStateOf(false)
        private set

    /** Counts every finished read, the cached ones included: the page reacts to each of them. */
    var reads by mutableStateOf(0)
        private set

    private val cache = HashMap<Pair<String, String>, GameInfo>()
    private var job: Job? = null

    private var requestId = 0

    /** Reads the file (or takes the answer from the cache). */
    fun read(file: File, keys: String) {
        val path = file.absolutePath
        val source = path to keys
        val request = ++requestId
        job?.cancel()
        val cached = cache[source]
        if (cached != null) {
            info = cached
            reading = false
            reads++
            return
        }
        info = null
        reading = true
        job = scope.launch {
            val result = runCatching { inspector.inspect(path, keys) }.getOrNull()
            if (request != requestId) return@launch   // the player picked another file: this answer is late
            if (result != null) cache[source] = result
            info = result
            reading = false
            reads++
        }
    }

    /** Nothing is selected any more. */
    fun clear() {
        requestId++
        job?.cancel()
        info = null
        reading = false
    }
}
