package com.diller.cartarchy.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalUriHandler
import androidx.lifecycle.viewmodel.compose.viewModel
import com.diller.cartarchy.game.GameViewModel
import com.diller.cartarchy.server.ServerGame
import com.diller.cartarchy.server.ServerViewModel

private enum class AppScreen { MENU, SETUP, SERVER, ABOUT }

@Composable
fun CartarchyApp(
    deepLink: String?,
    gameViewModel: GameViewModel = viewModel(),
    serverViewModel: ServerViewModel = viewModel(),
) {
    var screen by rememberSaveable { mutableStateOf(if (deepLink == null) AppScreen.MENU else AppScreen.SERVER) }
    val gameState = gameViewModel.uiState

    LaunchedEffect(deepLink) {
        if (!deepLink.isNullOrBlank()) {
            screen = AppScreen.SERVER
            serverViewModel.accept(deepLink)
        }
    }

    if (gameState.game != null && screen != AppScreen.SERVER) {
        GameScreen(
            gameViewModel,
            onCloseRemote = { serverViewModel.closeRemoteGame(gameViewModel) },
            onResignRemote = { serverViewModel.resignSelected(gameViewModel) },
        )
        return
    }

    BackHandler(enabled = screen != AppScreen.MENU) { screen = AppScreen.MENU }
    when (screen) {
        AppScreen.MENU -> MainMenu(
            hasSavedGame = gameState.hasSavedGame,
            onNewGame = { screen = AppScreen.SETUP },
            onContinue = gameViewModel::continueSavedGame,
            onServer = { screen = AppScreen.SERVER },
            onAbout = { screen = AppScreen.ABOUT },
            serverGames = serverViewModel.uiState.games,
            serverPlayerId = serverViewModel.uiState.playerId,
            serverBusy = serverViewModel.uiState.busy,
            onJoinGame = serverViewModel::joinGame,
            onStartGame = serverViewModel::startGame,
            onOpenGame = { serverViewModel.openGame(it, gameViewModel) },
            onRefreshGames = serverViewModel::refreshGames,
        )
        AppScreen.SETUP -> NewGameSetupScreen(
            onCancel = { screen = AppScreen.MENU },
            onBegin = gameViewModel::newGame,
        )
        AppScreen.SERVER -> ServerScreen(serverViewModel, onDone = { screen = AppScreen.MENU })
        AppScreen.ABOUT -> AboutScreen(onDone = { screen = AppScreen.MENU })
    }
}

@Composable
private fun MainMenu(
    hasSavedGame: Boolean,
    onNewGame: () -> Unit,
    onContinue: () -> Unit,
    onServer: () -> Unit,
    onAbout: () -> Unit,
    serverGames: List<ServerGame>,
    serverPlayerId: String,
    serverBusy: Boolean,
    onJoinGame: (String) -> Unit,
    onStartGame: (ServerGame) -> Unit,
    onOpenGame: (ServerGame) -> Unit,
    onRefreshGames: () -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxSize().background(PaperGray).statusBarsPadding().navigationBarsPadding().padding(28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Spacer(Modifier.height(24.dp))
        Text("Cartarchy", fontSize = 36.sp, fontWeight = FontWeight.Black, letterSpacing = (-2).sp)
        Text("A GAME OF GLOBAL STRATEGY", style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.Bold)
        HorizontalDivider(Modifier.padding(vertical = 20.dp).fillMaxWidth(.78f), thickness = 3.dp, color = Color.Black)
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(13.dp),
        ) {
            MacButton("NEW CAMPAIGN", onNewGame)
            MacButton("CONTINUE SAVED GAME", onContinue, enabled = hasSavedGame)
            MacButton("GAME SERVER…", onServer)
            serverGames.filter { it.isPlayable && it.isMember(serverPlayerId) }.forEach { game ->
                MacButton(game.playLabel.uppercase(), { onOpenGame(game) }, enabled = !serverBusy)
            }
            serverGames.filter { it.status == "waiting" }.forEach { game ->
                when {
                    !game.isMember(serverPlayerId) -> MacButton(
                        "JOIN ${game.name.uppercase()}",
                        { onJoinGame(game.id) },
                        enabled = !serverBusy && game.playerIds.size < game.maxPlayers,
                    )
                    game.hostId == serverPlayerId && game.playerIds.size >= 2 -> MacButton(
                        "START ${game.name.uppercase()}",
                        { onStartGame(game) },
                        enabled = !serverBusy,
                    )
                    else -> Text(
                        "WAITING: ${game.name.uppercase()}  ${game.playerIds.size}/${game.maxPlayers}",
                        Modifier.fillMaxWidth(),
                        style = MaterialTheme.typography.bodySmall,
                        textAlign = TextAlign.Center,
                        fontWeight = FontWeight.Bold,
                    )
                }
            }
            if (serverPlayerId.isNotEmpty()) {
                MacButton("REFRESH SERVER GAMES", onRefreshGames, enabled = !serverBusy)
            }
            MacButton("ABOUT / CREDITS", onAbout)
        }
        Spacer(Modifier.height(22.dp))
        Text("OFFLINE + SERVER EDITION", style = MaterialTheme.typography.bodySmall, textAlign = TextAlign.Center)
        Spacer(Modifier.height(12.dp))
        Text("ANDROID  •  PORTRAIT + LANDSCAPE", style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun AboutScreen(onDone: () -> Unit) {
    val uriHandler = LocalUriHandler.current
    Column(
        Modifier.fillMaxSize().background(PaperGray).statusBarsPadding().navigationBarsPadding().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text("ABOUT Cartarchy", fontSize = 24.sp, fontWeight = FontWeight.Black)
            HorizontalDivider(thickness = 3.dp, color = Color.Black)
            Text(
                "A native black-and-white game of global strategy for Android. Offline campaigns and optional self-hosted multiplayer are supported.",
                textAlign = TextAlign.Center,
            )
            Text(
                "Map geometry adapted from “Risk board.svg” by Gr0gmint under Creative Commons Attribution-ShareAlike 3.0. The 42 paths were renamed, transformed for mobile, and rendered with original monochrome interactions.",
                style = MaterialTheme.typography.bodySmall,
                textAlign = TextAlign.Center,
            )
            MacButton("MAP SOURCE", { uriHandler.openUri("https://commons.wikimedia.org/wiki/File:Risk_board.svg") })
            MacButton("CC BY-SA 3.0 LICENSE", { uriHandler.openUri("https://creativecommons.org/licenses/by-sa/3.0/") })
        }
        MacButton("DONE", onDone, Modifier.fillMaxWidth())
    }
}
