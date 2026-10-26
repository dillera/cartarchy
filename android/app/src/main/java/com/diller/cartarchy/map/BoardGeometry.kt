package com.diller.cartarchy.map

import android.content.Context
import android.graphics.RectF
import android.graphics.Region
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathFillType
import androidx.compose.ui.graphics.asAndroidPath
import com.diller.cartarchy.model.ClassicMap
import org.json.JSONObject
import kotlin.math.hypot

data class SeaRoute(val start: Offset, val end: Offset)

data class BoardPaths(
    val paths: Map<String, Path>,
    val centers: Map<String, Offset>,
) {
    fun territoryAt(point: Offset): String? = ClassicMap.territories.firstOrNull { territory ->
        paths[territory.id]?.containsPoint(point) == true
    }?.id
}

class BoardGeometry private constructor(private val pathData: Map<String, String>) {
    fun createPaths(size: Size): BoardPaths {
        val paths = pathData.mapValues { (_, source) -> SvgPathParser.parse(source, size) }
        val centers = paths.mapValues { (_, path) -> interiorCenter(path) }
        return BoardPaths(paths, centers)
    }

    val territoryIds: Set<String> get() = pathData.keys

    companion object {
        const val ASPECT_RATIO = 1000f / 650f
        private const val SOURCE_X = 167.99651f
        private const val SOURCE_Y = 118.55507f
        private const val SOURCE_WIDTH = 749.81909f
        private const val SOURCE_HEIGHT = 519.06781f

        val visibleSeaRoutes = listOf(
            SeaRoute(Offset(.333f, .150f), Offset(.423f, .225f)),
            SeaRoute(Offset(.386f, .618f), Offset(.468f, .585f)),
            SeaRoute(Offset(.500f, .430f), Offset(.480f, .565f)),
            SeaRoute(Offset(.585f, .445f), Offset(.585f, .565f)),
            SeaRoute(Offset(.655f, .555f), Offset(.625f, .665f)),
            SeaRoute(Offset(.785f, .620f), Offset(.805f, .735f)),
        )

        fun load(context: Context): BoardGeometry {
            val source = context.assets.open("board_paths.json").bufferedReader().use { it.readText() }
            val territories = JSONObject(source).getJSONObject("territories")
            val paths = territories.keys().asSequence().associateWith(territories::getString)
            return BoardGeometry(paths)
        }

        internal fun transform(x: Float, y: Float, size: Size) = Offset(
            (x - SOURCE_X) * size.width / SOURCE_WIDTH,
            (y - SOURCE_Y) * size.height / SOURCE_HEIGHT,
        )

        private fun interiorCenter(path: Path): Offset {
            val bounds = path.getBounds()
            val middle = bounds.center
            if (path.containsPoint(middle)) return middle
            var best: Offset? = null
            var bestDistance = Float.MAX_VALUE
            for (row in 1..11) {
                for (column in 1..11) {
                    val point = Offset(
                        bounds.left + bounds.width * column / 12f,
                        bounds.top + bounds.height * row / 12f,
                    )
                    if (!path.containsPoint(point)) continue
                    val distance = hypot(point.x - middle.x, point.y - middle.y)
                    if (distance < bestDistance) {
                        best = point
                        bestDistance = distance
                    }
                }
            }
            return best ?: middle
        }
    }
}

internal fun Path.containsPoint(point: Offset): Boolean {
    val androidPath = asAndroidPath()
    val bounds = RectF()
    androidPath.computeBounds(bounds, true)
    val clip = Region(
        kotlin.math.floor(bounds.left.toDouble()).toInt(),
        kotlin.math.floor(bounds.top.toDouble()).toInt(),
        kotlin.math.ceil(bounds.right.toDouble()).toInt(),
        kotlin.math.ceil(bounds.bottom.toDouble()).toInt(),
    )
    return Region().apply { setPath(androidPath, clip) }.contains(point.x.toInt(), point.y.toInt())
}

internal object SvgPathParser {
    private val tokenPattern = Regex("[MLCZmlcz]|[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?")

    fun parse(source: String, size: Size): Path {
        val tokens = tokenPattern.findAll(source).map { it.value }.toList()
        val path = Path().apply { fillType = PathFillType.EvenOdd }
        var index = 0
        var command: Char? = null

        fun isCommand(token: String) = token.length == 1 && token.first().isLetter()
        fun hasNumbers(count: Int) = index + count <= tokens.size && (0 until count).none { isCommand(tokens[index + it]) }
        fun point(): Offset {
            val x = tokens[index++].toFloat()
            val y = tokens[index++].toFloat()
            return BoardGeometry.transform(x, y, size)
        }

        while (index < tokens.size) {
            if (isCommand(tokens[index])) command = tokens[index++].first()
            when (command) {
                'M', 'm' -> {
                    if (!hasNumbers(2)) break
                    val first = point()
                    path.moveTo(first.x, first.y)
                    while (hasNumbers(2)) {
                        val next = point()
                        path.lineTo(next.x, next.y)
                    }
                }
                'L', 'l' -> while (hasNumbers(2)) {
                    val next = point()
                    path.lineTo(next.x, next.y)
                }
                'C', 'c' -> while (hasNumbers(6)) {
                    val control1 = point()
                    val control2 = point()
                    val end = point()
                    path.cubicTo(control1.x, control1.y, control2.x, control2.y, end.x, end.y)
                }
                'Z', 'z' -> {
                    path.close()
                    command = null
                }
                else -> break
            }
        }
        return path
    }
}
