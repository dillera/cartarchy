package com.diller.cartarchy.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.sp

val PaperGray = Color(0xFFE8E8E8)
val MediumGray = Color(0xFFC7C7C7)
val DarkGray = Color(0xFF555555)

private val MonochromeColors = lightColorScheme(
    primary = Color.Black,
    onPrimary = Color.White,
    secondary = DarkGray,
    onSecondary = Color.White,
    background = PaperGray,
    onBackground = Color.Black,
    surface = Color.White,
    onSurface = Color.Black,
    outline = Color.Black,
)

@Composable
fun CartarchyTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = MonochromeColors,
        typography = MaterialTheme.typography.copy(
            bodyLarge = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 16.sp),
            bodyMedium = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 14.sp),
            bodySmall = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 11.sp),
            titleLarge = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 24.sp),
            titleMedium = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 18.sp),
            labelLarge = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 13.sp),
        ),
        content = content,
    )
}
