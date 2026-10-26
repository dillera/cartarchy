package com.diller.cartarchy.game

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import com.diller.cartarchy.model.GameState
import com.diller.cartarchy.model.PlayerKind
import com.diller.cartarchy.model.TurnPhase
import com.diller.cartarchy.server.ServerGame

data class GameUiState(
    val game: GameState? = null,
    val selected: String? = null,
    val target: String? = null,
    val hasSavedGame: Boolean = false,
    val isRemote: Boolean = false,
    val isLocalTurn: Boolean = true,
    val remoteGameName: String = "",
    val localPlayerIndex: Int = 0,
)

class GameViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = GameRepository(application)
    private val engine = GameEngine()
    private var savedGame = repository.load()
    private var remoteGameId: String? = null
    private var remoteGameName = ""
    private var remoteVersion = 0
    private var remotePlayerIds: List<String> = emptyList()
    private var localPlayerIndex: Int? = null
    private var remoteWriteInFlight = false
    var onRemoteChange: ((GameState, String) -> Unit)? = null

    var uiState by mutableStateOf(GameUiState(hasSavedGame = savedGame != null))
        private set

    fun newGame(aiKinds: List<PlayerKind>) {
        resetRemote()
        engine.newGame(aiKinds)
        publish(save = true)
    }

    fun continueSavedGame() {
        resetRemote()
        savedGame?.let(engine::restore)
        publish(save = false)
    }

    fun openRemote(game: ServerGame, localPlayerId: String) {
        remoteGameId = game.id
        remoteGameName = game.name
        remoteVersion = game.version
        remotePlayerIds = game.playerIds
        localPlayerIndex = game.playerIds.indexOf(localPlayerId).takeIf { it >= 0 }
        remoteWriteInFlight = false
        if (game.state != null) {
            engine.restore(game.state)
        } else {
            engine.newRemoteGame(game.players.map { it.name })
        }
        game.currentPlayerId?.let { playerId ->
            remotePlayerIds.indexOf(playerId).takeIf { it >= 0 }?.let { engine.game?.currentPlayer = it }
        }
        publish(save = false)
    }

    fun applyRemote(game: ServerGame) {
        if (game.id != remoteGameId || game.version <= remoteVersion) return
        remoteVersion = game.version
        var updated = game.state?.deepCopy() ?: engine.game?.deepCopy() ?: return
        if (remotePlayerIds.size > game.playerIds.size) {
            remotePlayerIds.indices
                .filter { remotePlayerIds[it] !in game.playerIds && it in updated.players.indices }
                .forEach { updated.players[it].isAlive = false }
        }
        game.currentPlayerId?.let { playerId ->
            remotePlayerIds.indexOf(playerId).takeIf { it >= 0 }?.let { updated.currentPlayer = it }
        }
        engine.restore(updated)
        remoteWriteInFlight = false
        publish(save = false)
    }

    fun finishRemoteWrite() {
        remoteWriteInFlight = false
        publish(save = false)
    }

    fun closeRemote() {
        resetRemote()
        uiState = GameUiState(hasSavedGame = savedGame != null)
    }

    fun currentRemotePlayerId(): String? = engine.game?.currentPlayer
        ?.let { remotePlayerIds.getOrNull(it) }

    fun currentRemoteVersion(): Int = remoteVersion

    fun beginInitialRemoteWrite(): GameState? {
        val snapshot = engine.game?.deepCopy() ?: return null
        remoteWriteInFlight = true
        publish(save = false)
        return snapshot
    }

    fun remoteWinnerId(): String? {
        val game = engine.game ?: return null
        if (game.phase != TurnPhase.GAME_OVER) return null
        return remotePlayerIds.getOrNull(game.currentPlayer)
    }

    fun tapTerritory(id: String) {
        if (!canAct()) return
        if (engine.tap(id)) commit("state.updated")
    }

    fun nextPhase() {
        if (!canAct()) return
        val kind = if (engine.game?.phase == TurnPhase.FORTIFY) "turn.completed" else "state.updated"
        if (engine.nextPhase()) commit(kind)
    }

    fun tradeCards() {
        if (!canAct()) return
        if (engine.tradeCards()) commit("state.updated")
    }

    fun randomizeSetup() {
        if (!canAct()) return
        if (engine.randomizeSetup()) commit("state.updated")
    }

    fun quitGame() {
        if (remoteGameId != null) {
            closeRemote()
            return
        }
        repository.clear()
        savedGame = null
        uiState = GameUiState()
    }

    fun canTradeCards() = engine.canTradeCards()
    fun nextTradeValue() = engine.nextTradeValue()

    private fun canAct(): Boolean = remoteGameId == null ||
        (engine.game?.currentPlayer == localPlayerIndex && !remoteWriteInFlight)

    private fun commit(kind: String) {
        if (remoteGameId == null) {
            publish(save = true)
            return
        }
        val snapshot = engine.game?.deepCopy() ?: return
        remoteWriteInFlight = true
        publish(save = false)
        onRemoteChange?.invoke(snapshot, kind)
    }

    private fun publish(save: Boolean) {
        val snapshot = engine.game?.deepCopy()
        if (save && snapshot != null) {
            repository.save(snapshot)
            savedGame = snapshot.deepCopy()
        }
        uiState = GameUiState(
            game = snapshot,
            selected = engine.selected,
            target = engine.target,
            hasSavedGame = savedGame != null,
            isRemote = remoteGameId != null,
            isLocalTurn = remoteGameId == null || (snapshot?.currentPlayer == localPlayerIndex && !remoteWriteInFlight),
            remoteGameName = remoteGameName,
            localPlayerIndex = localPlayerIndex ?: 0,
        )
    }

    private fun resetRemote() {
        remoteGameId = null
        remoteGameName = ""
        remoteVersion = 0
        remotePlayerIds = emptyList()
        localPlayerIndex = null
        remoteWriteInFlight = false
        onRemoteChange = null
    }
}
