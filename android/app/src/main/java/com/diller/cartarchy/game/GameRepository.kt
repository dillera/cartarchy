package com.diller.cartarchy.game

import android.content.Context
import com.diller.cartarchy.model.GameState

class GameRepository(context: Context) {
    private val preferences = context.getSharedPreferences("cartarchy.game", Context.MODE_PRIVATE)

    fun load(): GameState? = preferences.getString(KEY, null)?.let { runCatching { GameCodec.decode(it) }.getOrNull() }

    fun save(game: GameState) {
        preferences.edit().putString(KEY, GameCodec.encode(game)).apply()
    }

    fun clear() {
        preferences.edit().remove(KEY).apply()
    }

    private companion object { const val KEY = "savedMatch.v1" }
}
