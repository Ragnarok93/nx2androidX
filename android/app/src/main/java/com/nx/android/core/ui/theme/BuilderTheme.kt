package com.nx.android.core.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.platform.LocalView
import androidx.activity.ComponentActivity
import androidx.core.view.WindowCompat
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

internal val LocalBuilderTheme = staticCompositionLocalOf { false }

private val Light = lightColorScheme(
    primary = Color(0xff1763dd), onPrimary = Color.White,
    primaryContainer = Color(0xffe5efff), onPrimaryContainer = Color(0xff034dae),
    secondaryContainer = Color(0xffe5efff), onSecondaryContainer = Color(0xff034dae),
    background = Color(0xfff3f3f5), onBackground = Color(0xff161619),
    surface = Color.White, onSurface = Color(0xff161619),
    surfaceVariant = Color(0xffececef), onSurfaceVariant = Color(0xff565660),
    surfaceContainer = Color(0xffececef), surfaceContainerHigh = Color(0xffe4e4e9),
    outline = Color(0xff80808a), outlineVariant = Color(0xffd9d9df),
)
private val Dark = darkColorScheme(
    primary = Color(0xff2169de), onPrimary = Color.White,
    primaryContainer = Color(0xff233552), onPrimaryContainer = Color(0xffa9caff),
    secondaryContainer = Color(0xff233552), onSecondaryContainer = Color(0xffa9caff),
    background = Color(0xff111113), onBackground = Color(0xfff6f6f8),
    surface = Color(0xff212124), onSurface = Color(0xfff6f6f8),
    surfaceVariant = Color(0xff29292e), onSurfaceVariant = Color(0xffbcbcc5),
    surfaceContainer = Color(0xff29292e), surfaceContainerHigh = Color(0xff343439),
    outline = Color(0xff8a8a95), outlineVariant = Color(0xff424249),
)

object BuilderSpacing {
    val screen = 16.dp
    val section = 16.dp
    val maxContentWidth = 900.dp
}

enum class BuilderThemeMode {
    SYSTEM, LIGHT, DARK;

    companion object {
        fun fromPreference(value: String): BuilderThemeMode = entries.firstOrNull { it.name == value } ?: SYSTEM
    }
}

@Composable
fun BuilderTheme(mode: BuilderThemeMode, content: @Composable () -> Unit) {
    val dark = when (mode) {
        BuilderThemeMode.SYSTEM -> isSystemInDarkTheme()
        BuilderThemeMode.LIGHT -> false
        BuilderThemeMode.DARK -> true
    }
    val colors = if (dark) Dark else Light
    val view = LocalView.current
    SideEffect {
        (view.context as? ComponentActivity)?.window?.let { window ->
            WindowCompat.getInsetsController(window, view).apply {
                isAppearanceLightStatusBars = !dark
                isAppearanceLightNavigationBars = !dark
            }
        }
    }
    MaterialTheme(
        colorScheme = colors,
        typography = Typography(),
        shapes = Shapes(medium = RoundedCornerShape(18.dp), large = RoundedCornerShape(28.dp)),
    ) {
        CompositionLocalProvider(LocalBuilderTheme provides true, LocalContentColor provides colors.onBackground) {
            content()
        }
    }
}
