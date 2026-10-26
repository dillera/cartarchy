package com.diller.cartarchy.ui

enum class GameLayoutMode { STACKED, SPLIT }

object AdaptiveLayoutPolicy {
    fun gameMode(width: Float, height: Float): GameLayoutMode =
        if (width > height) GameLayoutMode.SPLIT else GameLayoutMode.STACKED

    fun consoleWidth(totalWidth: Float): Float = (totalWidth * 0.375f).coerceIn(320f, 440f)
}
