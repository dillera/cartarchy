package com.diller.cartarchy.ui

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Fill
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.diller.cartarchy.map.BoardGeometry
import com.diller.cartarchy.map.BoardPaths
import com.diller.cartarchy.model.ClassicMap
import com.diller.cartarchy.model.GameState

@Composable
fun BoardView(
    game: GameState,
    selected: String?,
    onTerritory: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val geometry = remember { BoardGeometry.load(context) }
    var boardPaths by remember { mutableStateOf<BoardPaths?>(null) }
    val markerPaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = android.graphics.Color.BLACK
            textAlign = Paint.Align.CENTER
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
        }
    }

    Canvas(
        modifier
            .aspectRatio(BoardGeometry.ASPECT_RATIO)
            .background(PaperGray)
            .border(2.dp, Color.Black)
            .onSizeChanged { pixels -> boardPaths = geometry.createPaths(Size(pixels.width.toFloat(), pixels.height.toFloat())) }
            .pointerInput(boardPaths) {
                detectTapGestures { point -> boardPaths?.territoryAt(point)?.let(onTerritory) }
            },
    ) {
        val paths = boardPaths ?: return@Canvas
        ClassicMap.territories.forEach { territory ->
            val path = paths.paths[territory.id] ?: return@forEach
            val owner = game.territories[territory.id]?.owner ?: -1
            drawPath(path, ownerFill(owner), style = Fill)
            if (owner >= 2) drawOwnerPattern(path, owner)
            drawPath(path, Color.Black, style = Stroke(width = if (selected == territory.id) 4.dp.toPx() else 1.2.dp.toPx()))
        }

        val dash = PathEffect.dashPathEffect(floatArrayOf(7.dp.toPx(), 5.dp.toPx()))
        BoardGeometry.visibleSeaRoutes.forEach { route ->
            drawLine(
                Color.Black,
                Offset(route.start.x * size.width, route.start.y * size.height),
                Offset(route.end.x * size.width, route.end.y * size.height),
                1.6.dp.toPx(),
                pathEffect = dash,
            )
        }
        drawLine(Color.Black, Offset(0f, size.height * .22f), Offset(size.width * .055f, size.height * .22f), 1.6.dp.toPx(), pathEffect = dash)
        drawLine(Color.Black, Offset(size.width * .94f, size.height * .22f), Offset(size.width, size.height * .22f), 1.6.dp.toPx(), pathEffect = dash)

        markerPaint.textSize = 10.dp.toPx()
        ClassicMap.territories.forEach { territory ->
            val armies = game.territories[territory.id]?.armies ?: 0
            if (armies <= 0) return@forEach
            val center = paths.centers[territory.id] ?: return@forEach
            drawCircle(Color.White, 10.dp.toPx(), center)
            drawCircle(Color.Black, 10.dp.toPx(), center, style = Stroke(1.4.dp.toPx()))
            val baseline = center.y - (markerPaint.ascent() + markerPaint.descent()) / 2f
            drawContext.canvas.nativeCanvas.drawText(armies.toString(), center.x, baseline, markerPaint)
        }
    }
}

@Composable
fun TerritoryPatternSwatch(owner: Int, modifier: Modifier = Modifier) {
    Canvas(modifier.background(PaperGray).border(2.dp, Color.Black)) {
        val swatch = Path().apply { addRect(Rect(Offset.Zero, size)) }
        drawPath(swatch, ownerFill(owner), style = Fill)
        if (owner >= 2) drawOwnerPattern(swatch, owner)
    }
}

private fun ownerFill(owner: Int): Color = when (owner) {
    -1 -> PaperGray
    0 -> Color.White
    1 -> Color(0xFF161616)
    2 -> Color(0xFFD0D0D0)
    3 -> Color.White
    4 -> Color(0xFF8A8A8A)
    else -> Color(0xFFE0E0E0)
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawOwnerPattern(path: Path, owner: Int) {
    val spacing = when (owner) { 2 -> 8.dp.toPx(); 3 -> 11.dp.toPx(); else -> 7.dp.toPx() }
    val stroke = if (owner == 2) 1.dp.toPx() else 1.5.dp.toPx()
    clipPath(path) {
        var x = -size.height
        while (x <= size.width) {
            drawLine(Color.Black.copy(alpha = if (owner == 2) .45f else .8f), Offset(x, 0f), Offset(x + size.height, size.height), stroke)
            x += spacing
        }
        if (owner >= 5) {
            var reverseX = 0f
            while (reverseX <= size.width + size.height) {
                drawLine(Color.Black.copy(alpha = .55f), Offset(reverseX, 0f), Offset(reverseX - size.height, size.height), 1.dp.toPx())
                reverseX += spacing
            }
        }
    }
}
