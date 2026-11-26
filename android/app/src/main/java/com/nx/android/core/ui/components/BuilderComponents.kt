package com.nx.android.core.ui.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import com.nx.android.core.ui.theme.BuilderSpacing

@Composable
fun BuilderPage(content: @Composable ColumnScope.() -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        Column(
            Modifier.widthIn(max = BuilderSpacing.maxContentWidth).fillMaxWidth()
                .verticalScroll(rememberPageScroll()).padding(BuilderSpacing.screen),
            verticalArrangement = Arrangement.spacedBy(BuilderSpacing.section),
            content = content,
        )
    }
}

@Composable
fun BuilderListRow(title: String, summary: String? = null, onClick: (() -> Unit)? = null) {
    ListItem(
        modifier = if (onClick == null) Modifier else Modifier.clickable(onClick = onClick),
        headlineContent = { Text(title) },
        supportingContent = summary?.let { { Text(it) } },
        trailingContent = if (onClick == null) null else {
            { Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, contentDescription = null) }
        },
        colors = ListItemDefaults.colors(
            containerColor = Color.Transparent,
            supportingColor = MaterialTheme.colorScheme.onSurfaceVariant,
        ),
    )
}
