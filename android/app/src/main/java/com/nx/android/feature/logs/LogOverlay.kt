package com.nx.android.feature.logs

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nx.android.R
import com.nx.android.core.ui.components.QuietButton
import com.nx.android.core.ui.theme.GlassSheet
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private const val PAGE_BYTES = 64 * 1024

@Composable
internal fun LogOverlay(
    onClose: () -> Unit,
    text: String,
    title: String? = null,
    buildId: String? = null,
    initialOffset: Long = 0,
    initialLength: Long = 0,
    active: Boolean = false,
    emptyText: String? = null,
    readRange: suspend (Long, Int) -> String = { _, _ -> "" },
    latestLength: suspend () -> Long = { initialLength },
) {
    var content by remember(buildId) { mutableStateOf(text) }
    var offset by remember(buildId) { mutableLongStateOf(initialOffset) }
    var fileLength by remember(buildId) { mutableLongStateOf(initialLength) }
    var following by remember(buildId) { mutableStateOf(active) }
    val scroll = rememberScrollState()
    var seeking by remember(buildId) { mutableStateOf(false) }
    var autoScrolling by remember(buildId) { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    LaunchedEffect(buildId, text, initialOffset, initialLength) {
        content = text
        offset = initialOffset
        fileLength = initialLength
    }
    LaunchedEffect(active, buildId) {
        if (buildId != null && active) {
            while (true) {
                delay(800)
                val length = withContext(Dispatchers.IO) { latestLength() }
                fileLength = length
                if (following && !seeking && length > 0) {
                    val start = (length - PAGE_BYTES).coerceAtLeast(0)
                    val latest = withContext(Dispatchers.IO) { readRange(start, PAGE_BYTES) }
                    if (following && !seeking) {
                        content = latest
                        offset = start
                    }
                }
            }
        }
    }
    LaunchedEffect(content, following) {
        if (following) {
            autoScrolling = true
            try {
                withFrameNanos { }
                scroll.scrollTo(Int.MAX_VALUE)
            } finally {
                autoScrolling = false
            }
        }
    }
    LaunchedEffect(scroll, buildId) {
        snapshotFlow {
            if (scroll.isScrollInProgress && !autoScrolling && !seeking)
                scroll.value >= scroll.maxValue - 24 else null
        }.collect { nearEnd -> if (nearEnd != null) following = nearEnd }
    }

    Box(
        Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.6f)).clickable { onClose() },
        contentAlignment = Alignment.Center,
    ) {
        GlassSheet(modifier = Modifier.fillMaxSize(0.9f).clickable(enabled = false) {}, alpha = 0.97f) {
            Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(title ?: stringResource(R.string.common_log), style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                    QuietButton(stringResource(R.string.common_close), onClose)
                }
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                if (buildId != null) {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        if (offset > 0) TextButton(enabled = !seeking, onClick = {
                            val start = (offset - PAGE_BYTES).coerceAtLeast(0)
                            val bytes = (offset - start).toInt()
                            seeking = true
                            following = false
                            scope.launch {
                                try {
                                    val earlier = withContext(Dispatchers.IO) { readRange(start, bytes) }
                                    content = earlier + content
                                    offset = start
                                } finally { seeking = false }
                            }
                        }) { Text(stringResource(R.string.builds_log_earlier)) }
                        if (active && !following) TextButton(enabled = !seeking, onClick = {
                            seeking = true
                            scope.launch {
                                try {
                                    val length = withContext(Dispatchers.IO) { latestLength() }
                                    val start = (length - PAGE_BYTES).coerceAtLeast(0)
                                    content = withContext(Dispatchers.IO) { readRange(start, PAGE_BYTES) }
                                    offset = start
                                    fileLength = length
                                    following = true
                                } finally { seeking = false }
                            }
                        }) { Text(stringResource(R.string.builds_log_latest)) }
                    }
                }
                Box(
                    Modifier.fillMaxWidth().weight(1f).verticalScroll(scroll)
                        .background(Color.Black.copy(alpha = 0.3f), RoundedCornerShape(12.dp)).padding(10.dp)
                ) {
                    Text(
                        content.ifEmpty { emptyText ?: stringResource(R.string.main_log_empty) },
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                        fontSize = 11.sp,
                    )
                }
                if (buildId != null && offset + content.toByteArray(Charsets.UTF_8).size < fileLength) {
                    TextButton(enabled = !seeking, onClick = {
                        val nextOffset = offset + content.toByteArray(Charsets.UTF_8).size
                        val size = (fileLength - nextOffset).coerceAtMost(PAGE_BYTES.toLong()).toInt()
                        seeking = true
                        following = false
                        scope.launch {
                            try {
                                content += withContext(Dispatchers.IO) { readRange(nextOffset, size) }
                            } finally { seeking = false }
                        }
                    }) { Text(stringResource(R.string.builds_log_later)) }
                }
            }
        }
    }
}
