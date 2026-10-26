package com.diller.cartarchy.server

import android.app.Application
import android.content.Context
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.diller.cartarchy.game.GameCodec
import com.diller.cartarchy.game.GameViewModel
import com.diller.cartarchy.model.GameState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

data class ServerGame(
    val id: String,
    val name: String,
    val mode: String,
    val status: String,
    val hostId: String?,
    val playerIds: List<String>,
    val players: List<ServerPlayer>,
    val currentPlayerId: String?,
    val winnerId: String?,
    val maxPlayers: Int,
    val currentRound: Int,
    val currentTurn: Int,
    val version: Int,
    val state: GameState?,
    val createdAt: String,
    val updatedAt: String,
) {
    val playLabel: String get() = (if (state == null) "PLAY " else "RESUME ") + name
    val isPlayable: Boolean get() = status == "active" || status == "paused"
    fun isMember(playerId: String) = playerId.isNotEmpty() && playerId in playerIds
}

data class ServerPlayer(val id: String, val name: String)

data class ServerUiState(
    val serverUrl: String = "",
    val pairingCode: String = "",
    val playerName: String = "Commander",
    val playerId: String = "",
    val token: String = "",
    val profileUrl: String = "",
    val status: String = "NOT CONFIGURED",
    val pendingGameId: String = "",
    val games: List<ServerGame> = emptyList(),
    val busy: Boolean = false,
) {
    val isConnected: Boolean get() = token.isNotEmpty()
}

class ServerViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = application.getSharedPreferences("cartarchy.server", Context.MODE_PRIVATE)
    private val deviceId = preferences.getString("device.id", null) ?: java.util.UUID.randomUUID().toString()
    private val liveSyncPolicy = LiveSyncPolicy()
    private var applicationActive = false
    private var liveUpdatesJob: Job? = null
    private var pollingJob: Job? = null
    private var refreshJob: Job? = null
    private var refreshAgain = false
    @Volatile private var eventConnection: HttpURLConnection? = null
    private var selectedGameId: String? = null
    private var activeGameViewModel: GameViewModel? = null

    var uiState by mutableStateOf(
        ServerUiState(
            serverUrl = preferences.getString("server.url", "") ?: "",
            playerName = preferences.getString("player.name", "Commander") ?: "Commander",
            playerId = preferences.getString("player.id", "") ?: "",
            token = preferences.getString("server.token", "") ?: "",
            profileUrl = preferences.getString("player.profileUrl", "") ?: "",
            status = if (preferences.getString("server.token", "").isNullOrEmpty()) "NOT CONFIGURED" else "CONNECTED",
        ),
    )
        private set

    init {
        preferences.edit().putString("device.id", deviceId).apply()
        if (uiState.isConnected) refreshGames()
    }

    fun updateServerUrl(value: String) { uiState = uiState.copy(serverUrl = value) }
    fun updatePairingCode(value: String) { uiState = uiState.copy(pairingCode = value) }
    fun updatePlayerName(value: String) { uiState = uiState.copy(playerName = value) }
    fun reportScannerError(message: String) { uiState = uiState.copy(status = message.uppercase()) }

    fun accept(scannedValue: String) {
        val uri = runCatching { Uri.parse(scannedValue) }.getOrNull()
        if (uri?.scheme != "cartarchy") {
            uiState = uiState.copy(status = "NOT A CARTARCHY CODE")
            return
        }
        when (uri.host) {
            "configure" -> {
                val server = uri.getQueryParameter("server")
                val code = uri.getQueryParameter("code")
                if (server.isNullOrBlank() || code.isNullOrBlank()) {
                    uiState = uiState.copy(status = "INCOMPLETE CODE")
                    return
                }
                uiState = uiState.copy(serverUrl = server, pairingCode = code, pendingGameId = "")
                register()
            }
            "join" -> {
                val server = uri.getQueryParameter("server")
                val game = uri.getQueryParameter("game")
                val pairing = uri.getQueryParameter("pairing")
                if (server.isNullOrBlank() || game.isNullOrBlank() || pairing.isNullOrBlank()) {
                    uiState = uiState.copy(status = "INCOMPLETE GAME CODE")
                    return
                }
                val sameServer = normalizeServer(uiState.serverUrl) == normalizeServer(server)
                if (!sameServer) stopLiveSync()
                uiState = uiState.copy(
                    serverUrl = server,
                    pairingCode = pairing,
                    pendingGameId = game,
                    token = if (sameServer) uiState.token else "",
                )
                if (uiState.token.isEmpty()) register() else joinGame(game)
            }
            else -> uiState = uiState.copy(status = "UNKNOWN CARTARCHY CODE")
        }
    }

    fun register() {
        stopLiveSync()
        val snapshot = uiState
        if (runCatching { URL(normalizeServer(snapshot.serverUrl)) }.isFailure) {
            uiState = snapshot.copy(status = "INVALID SERVER URL")
            return
        }
        viewModelScope.launch {
            uiState = uiState.copy(status = "CONNECTING…", busy = true)
            runCatching {
                val payload = JSONObject()
                    .put("DeviceID", deviceId)
                    .put("Name", snapshot.playerName)
                    .put("PairingCode", snapshot.pairingCode)
                request("POST", "/v1/devices/register", payload, token = null, expected = 201)
            }.onSuccess { response ->
                val player = response.getJSONObject("player")
                val token = response.getString("token")
                val name = player.getString("name")
                val playerId = player.getString("id")
                val profile = response.optString("profileURL")
                uiState = uiState.copy(
                    token = token,
                    playerName = name,
                    playerId = playerId,
                    profileUrl = profile,
                    status = "CONNECTED AS ${name.uppercase()}",
                    busy = false,
                )
                saveConnection()
                val pending = uiState.pendingGameId
                if (pending.isNotEmpty()) joinGame(pending) else refreshGames()
                startLiveSync()
            }.onFailure { failure ->
                uiState = uiState.copy(status = failure.message?.uppercase() ?: "PAIRING REJECTED", busy = false)
            }
        }
    }

    fun joinGame(gameId: String) {
        viewModelScope.launch {
            uiState = uiState.copy(status = "JOINING GAME…", busy = true)
            runCatching {
                request("POST", "/v1/games/${Uri.encode(gameId)}/join", JSONObject(), uiState.token, 200)
            }.onSuccess { game ->
                uiState = uiState.copy(
                    pendingGameId = "",
                    status = "JOINED ${game.getString("name").uppercase()}",
                    busy = false,
                )
                refreshGames()
            }.onFailure { failure ->
                uiState = uiState.copy(status = failure.message?.uppercase() ?: "GAME JOIN REJECTED", busy = false)
            }
        }
    }

    fun startGame(game: ServerGame) {
        viewModelScope.launch {
            uiState = uiState.copy(status = "STARTING ${game.name.uppercase()}…", busy = true)
            runCatching {
                request("POST", "/v1/games/${Uri.encode(game.id)}/start", JSONObject(), uiState.token, 200)
            }.onSuccess { response ->
                val started = serverGameFromJson(response)
                replaceGame(started)
                uiState = uiState.copy(status = "STARTED ${started.name.uppercase()}", busy = false)
            }.onFailure { failure ->
                uiState = uiState.copy(status = failure.message?.uppercase() ?: "GAME START FAILED", busy = false)
            }
        }
    }

    fun openGame(game: ServerGame, gameViewModel: GameViewModel) {
        viewModelScope.launch {
            uiState = uiState.copy(status = "OPENING ${game.name.uppercase()}…", busy = true)
            val opened = if (game.status == "paused") {
                runCatching {
                    request("POST", "/v1/games/${Uri.encode(game.id)}/resume", JSONObject(), uiState.token, 200)
                }.getOrElse { failure ->
                    uiState = uiState.copy(status = failure.message?.uppercase() ?: "RESUME FAILED", busy = false)
                    return@launch
                }.let(::serverGameFromJson)
            } else {
                game
            }
            replaceGame(opened)
            selectedGameId = opened.id
            activeGameViewModel = gameViewModel
            gameViewModel.openRemote(opened, uiState.playerId)
            gameViewModel.onRemoteChange = { state, kind ->
                viewModelScope.launch { pushRemoteState(state, kind, gameViewModel) }
            }
            uiState = uiState.copy(status = "PLAYING ${opened.name.uppercase()}", busy = false)
            if (opened.state == null && opened.currentPlayerId == uiState.playerId) {
                gameViewModel.beginInitialRemoteWrite()?.let { state ->
                    pushRemoteState(state, "state.initialized", gameViewModel)
                }
            }
        }
    }

    fun closeRemoteGame(gameViewModel: GameViewModel) {
        selectedGameId = null
        activeGameViewModel = null
        gameViewModel.closeRemote()
    }

    fun resignSelected(gameViewModel: GameViewModel) {
        val gameId = selectedGameId ?: return
        viewModelScope.launch {
            uiState = uiState.copy(status = "RESIGNING GAME…", busy = true)
            runCatching {
                request("POST", "/v1/games/${Uri.encode(gameId)}/resign", JSONObject(), uiState.token, 200)
            }.onSuccess {
                selectedGameId = null
                activeGameViewModel = null
                gameViewModel.closeRemote()
                uiState = uiState.copy(status = "RESIGNED GAME", busy = false)
                refreshGames()
            }.onFailure { failure ->
                uiState = uiState.copy(status = failure.message?.uppercase() ?: "RESIGN FAILED", busy = false)
            }
        }
    }

    fun refreshGames() {
        if (!uiState.isConnected) return
        if (refreshJob?.isActive == true) {
            refreshAgain = true
            return
        }
        refreshJob = viewModelScope.launch {
            do {
                refreshAgain = false
                if (uiState.playerId.isEmpty()) restoreIdentity()
                runCatching { requestArray("GET", "/v1/games", uiState.token) }
                    .onSuccess { games ->
                        val fetched = games.map(::serverGameFromJson)
                        uiState = uiState.copy(games = fetched, busy = false)
                        selectedGameId?.let { selected ->
                            fetched.firstOrNull { it.id == selected }?.let { activeGameViewModel?.applyRemote(it) }
                        }
                    }
                    .onFailure { failure -> uiState = uiState.copy(status = failure.message?.uppercase() ?: "REFRESH FAILED", busy = false) }
            } while (refreshAgain && uiState.isConnected)
        }
    }

    fun disconnect() {
        stopLiveSync()
        activeGameViewModel?.closeRemote()
        activeGameViewModel = null
        selectedGameId = null
        preferences.edit().remove("server.token").remove("player.id").apply()
        uiState = uiState.copy(token = "", playerId = "", pairingCode = "", pendingGameId = "", games = emptyList(), status = "NOT CONFIGURED")
    }

    fun setApplicationActive(active: Boolean) {
        applicationActive = active
        if (active) {
            if (liveSyncPolicy.refreshOnForeground) refreshGames()
            startLiveSync()
        } else {
            stopLiveSync()
        }
    }

    private fun startLiveSync() {
        if (!applicationActive || !uiState.isConnected) return
        if (liveUpdatesJob?.isActive != true) {
            liveUpdatesJob = viewModelScope.launch { runEventLoop() }
        }
        if (pollingJob?.isActive != true) {
            pollingJob = viewModelScope.launch {
                while (currentCoroutineContext().isActive && applicationActive && uiState.isConnected) {
                    delay(liveSyncPolicy.pollIntervalSeconds * 1_000L)
                    if (applicationActive && uiState.isConnected) refreshGames()
                }
            }
        }
    }

    private fun stopLiveSync() {
        liveUpdatesJob?.cancel(); liveUpdatesJob = null
        pollingJob?.cancel(); pollingJob = null
        refreshJob?.cancel(); refreshJob = null; refreshAgain = false
        eventConnection?.disconnect(); eventConnection = null
    }

    private suspend fun runEventLoop() {
        var attempt = 0
        while (currentCoroutineContext().isActive && applicationActive && uiState.isConnected) {
            try {
                val receivedEvent = consumeEventStream()
                attempt = if (receivedEvent) 0 else attempt + 1
            } catch (_: CancellationException) {
                return
            } catch (_: Exception) {
                if (!currentCoroutineContext().isActive) return
                attempt += 1
                uiState = uiState.copy(status = "LIVE UPDATES RECONNECTING…")
            }
            if (!currentCoroutineContext().isActive || !applicationActive || !uiState.isConnected) return
            delay(liveSyncPolicy.reconnectDelaySeconds((attempt - 1).coerceAtLeast(0)) * 1_000L)
        }
    }

    private suspend fun consumeEventStream(): Boolean = withContext(Dispatchers.IO) {
        val connection = URL(normalizeServer(uiState.serverUrl) + "/v1/events").openConnection() as HttpURLConnection
        eventConnection = connection
        try {
            connection.requestMethod = "GET"
            connection.connectTimeout = 8_000
            connection.readTimeout = 0
            connection.setRequestProperty("Accept", "text/event-stream")
            connection.setRequestProperty("Authorization", "Bearer ${uiState.token}")
            val status = connection.responseCode
            if (status != 200) {
                val text = connection.errorStream?.bufferedReader()?.use { it.readText() } ?: ""
                throw IllegalStateException(problemMessage(status, text))
            }
            var receivedEvent = false
            connection.inputStream.bufferedReader().use { reader ->
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val line = reader.readLine() ?: break
                    if (!line.startsWith("data: ")) continue
                    receivedEvent = true
                    withContext(Dispatchers.Main.immediate) {
                        refreshGames()
                        if (uiState.status == "LIVE UPDATES RECONNECTING…") {
                            uiState = uiState.copy(status = "CONNECTED AS ${uiState.playerName.uppercase()}")
                        }
                    }
                }
            }
            receivedEvent
        } finally {
            if (eventConnection === connection) eventConnection = null
            connection.disconnect()
        }
    }

    override fun onCleared() {
        stopLiveSync()
        super.onCleared()
    }

    private fun saveConnection() {
        preferences.edit()
            .putString("server.url", normalizeServer(uiState.serverUrl))
            .putString("server.token", uiState.token)
            .putString("player.name", uiState.playerName)
            .putString("player.id", uiState.playerId)
            .putString("player.profileUrl", uiState.profileUrl)
            .apply()
    }

    private suspend fun restoreIdentity() {
        if (uiState.playerId.isNotEmpty() || !uiState.isConnected) return
        runCatching { request("GET", "/v1/stats", null, uiState.token, 200) }
            .onSuccess { player ->
                val id = player.optString("id")
                if (id.isNotEmpty()) {
                    uiState = uiState.copy(playerId = id)
                    preferences.edit().putString("player.id", id).apply()
                }
            }
    }

    private suspend fun pushRemoteState(state: GameState, kind: String, gameViewModel: GameViewModel) {
        val gameId = selectedGameId ?: return gameViewModel.finishRemoteWrite()
        val body = JSONObject()
            .put("ExpectedVersion", gameViewModel.currentRemoteVersion())
            .put("Kind", kind)
            .put("Payload", JSONObject().put("currentPlayerId", gameViewModel.currentRemotePlayerId() ?: ""))
            .put("State", GameCodec.encodeServer(state))
        runCatching {
            request("POST", "/v1/games/${Uri.encode(gameId)}/events", body, uiState.token, 200)
        }.onSuccess { response ->
            val updated = serverGameFromJson(response)
            replaceGame(updated)
            gameViewModel.applyRemote(updated)
            gameViewModel.remoteWinnerId()?.let { winnerId -> finishGame(updated.id, winnerId, gameViewModel) }
        }.onFailure { failure ->
            uiState = uiState.copy(status = failure.message?.uppercase() ?: "SYNC FAILED")
            refreshGames()
        }
        gameViewModel.finishRemoteWrite()
    }

    private suspend fun finishGame(gameId: String, winnerId: String, gameViewModel: GameViewModel) {
        val body = JSONObject().put("WinnerID", winnerId)
        runCatching {
            request("POST", "/v1/games/${Uri.encode(gameId)}/finish", body, uiState.token, 200)
        }.onSuccess { response ->
            val finished = serverGameFromJson(response)
            replaceGame(finished)
            gameViewModel.applyRemote(finished)
            uiState = uiState.copy(status = "GAME COMPLETE")
        }.onFailure { refreshGames() }
    }

    private fun replaceGame(game: ServerGame) {
        val games = uiState.games.toMutableList()
        val index = games.indexOfFirst { it.id == game.id }
        if (index >= 0) games[index] = game else games += game
        uiState = uiState.copy(games = games)
    }

    private suspend fun request(
        method: String,
        path: String,
        body: JSONObject?,
        token: String?,
        expected: Int,
    ): JSONObject = withContext(Dispatchers.IO) {
        val (status, text) = execute(method, path, body, token)
        if (status != expected) throw IllegalStateException(problemMessage(status, text))
        JSONObject(text)
    }

    private suspend fun requestArray(method: String, path: String, token: String): List<JSONObject> = withContext(Dispatchers.IO) {
        val (status, text) = execute(method, path, null, token)
        if (status != 200) throw IllegalStateException(problemMessage(status, text))
        val array = JSONArray(text)
        List(array.length()) { array.getJSONObject(it) }
    }

    private fun execute(method: String, path: String, body: JSONObject?, token: String?): Pair<Int, String> {
        val connection = URL(normalizeServer(uiState.serverUrl) + path).openConnection() as HttpURLConnection
        return try {
            connection.requestMethod = method
            connection.connectTimeout = 8_000
            connection.readTimeout = 12_000
            connection.setRequestProperty("Accept", "application/json")
            if (!token.isNullOrBlank()) connection.setRequestProperty("Authorization", "Bearer $token")
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.bufferedWriter().use { it.write(body.toString()) }
            }
            val status = connection.responseCode
            val stream = if (status in 200..299) connection.inputStream else connection.errorStream
            status to (stream?.bufferedReader()?.use { it.readText() } ?: "")
        } finally {
            connection.disconnect()
        }
    }

    private fun problemMessage(status: Int, text: String): String = runCatching {
        JSONObject(text).optString("error").takeIf { it.isNotBlank() }
    }.getOrNull() ?: "SERVER ERROR $status"

    private fun normalizeServer(value: String) = value.trim().trimEnd('/')
}

internal fun serverGameFromJson(json: JSONObject): ServerGame {
    fun strings(key: String): List<String> = json.optJSONArray(key)?.let { array ->
        List(array.length()) { array.getString(it) }
    } ?: emptyList()
    val players = json.optJSONArray("players")?.let { array ->
        List(array.length()) { index ->
            val player = array.getJSONObject(index)
            ServerPlayer(player.getString("id"), player.getString("name"))
        }
    } ?: emptyList()
    return ServerGame(
        id = json.getString("id"),
        name = json.getString("name"),
        mode = json.getString("mode"),
        status = json.getString("status"),
        hostId = json.optString("hostId").takeIf(String::isNotEmpty),
        playerIds = strings("playerIds"),
        players = players,
        currentPlayerId = json.optString("currentPlayerId").takeIf(String::isNotEmpty),
        winnerId = json.optString("winnerId").takeIf(String::isNotEmpty),
        maxPlayers = json.getInt("maxPlayers"),
        currentRound = json.optInt("currentRound"),
        currentTurn = json.optInt("currentTurn"),
        version = json.getInt("version"),
        state = json.optJSONObject("state")?.let(GameCodec::decodeServer),
        createdAt = json.optString("createdAt"),
        updatedAt = json.optString("updatedAt"),
    )
}
