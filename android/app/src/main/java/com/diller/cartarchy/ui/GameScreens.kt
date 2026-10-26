package com.diller.cartarchy.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.TextButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.diller.cartarchy.game.GameViewModel
import com.diller.cartarchy.model.GameState
import com.diller.cartarchy.model.PlayerKind
import com.diller.cartarchy.model.TurnPhase

@Composable
fun NewGameSetupScreen(
    onCancel: () -> Unit,
    onBegin: (List<PlayerKind>) -> Unit,
) {
    val choices = remember { listOf(PlayerKind.CAUTIOUS, PlayerKind.BALANCED, PlayerKind.AGGRESSIVE) }
    var aiKindNames by rememberSaveable { mutableStateOf(listOf("CAUTIOUS", "BALANCED", "AGGRESSIVE")) }
    val aiKinds = aiKindNames.map(PlayerKind::valueOf)

    Column(
        Modifier.fillMaxSize().background(PaperGray).statusBarsPadding().navigationBarsPadding().padding(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(13.dp),
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            MacButton("‹ BACK", onCancel, Modifier.weight(.3f))
            Text("NEW CAMPAIGN", Modifier.weight(.7f), textAlign = TextAlign.Center, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        }
        Text("SELECT COMPUTER COMMANDERS", style = MaterialTheme.typography.bodySmall)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            MacButton("−", { if (aiKinds.size > 1) aiKindNames = aiKindNames.dropLast(1) }, Modifier.weight(.2f), aiKinds.size > 1)
            Text("${aiKinds.size} AI  •  ${aiKinds.size + 1} PLAYERS", Modifier.weight(.6f), textAlign = TextAlign.Center, fontWeight = FontWeight.Bold)
            MacButton("+", { if (aiKinds.size < 5) aiKindNames = aiKindNames + choices[aiKinds.size % choices.size].name }, Modifier.weight(.2f), aiKinds.size < 5)
        }
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            CommanderRow("COMMANDER", "HUMAN")
            aiKinds.forEachIndexed { index, kind ->
                CommanderRow("CPU ${index + 1}", kind.label.uppercase()) {
                    val next = choices[(choices.indexOf(kind) + 1) % choices.size]
                    aiKindNames = aiKindNames.toMutableList().also { it[index] = next.name }
                }
            }
        }
        Text(
            "Territories and starting armies are placed one at a time in player order.",
            style = MaterialTheme.typography.bodySmall,
            textAlign = TextAlign.Center,
        )
        MacButton("BEGIN PLACEMENT", { onBegin(aiKinds) }, Modifier.fillMaxWidth())
    }
}

@Composable
private fun CommanderRow(name: String, kind: String, onKindClick: (() -> Unit)? = null) {
    Row(
        Modifier.fillMaxWidth().background(Color.White).border(2.dp, Color.Black).padding(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(name, Modifier.weight(1f), fontWeight = FontWeight.Bold)
        if (onKindClick == null) {
            Text(kind, style = MaterialTheme.typography.bodySmall)
        } else {
            MacButton(kind, onKindClick, Modifier.weight(1.25f))
        }
    }
}

@Composable
fun GameScreen(
    viewModel: GameViewModel,
    onCloseRemote: () -> Unit = {},
    onResignRemote: () -> Unit = {},
) {
    val uiState = viewModel.uiState
    val game = uiState.game ?: return
    var confirmingResignation by rememberSaveable { mutableStateOf(false) }
    BoxWithConstraints(Modifier.fillMaxSize().background(PaperGray).statusBarsPadding().navigationBarsPadding()) {
        val availableWidth = maxWidth
        val split = AdaptiveLayoutPolicy.gameMode(maxWidth.value, maxHeight.value) == GameLayoutMode.SPLIT
        Column(Modifier.fillMaxSize()) {
            StatusBar(game)
            if (split) {
                val consoleWidth = AdaptiveLayoutPolicy.consoleWidth(availableWidth.value).dp
                Row(Modifier.weight(1f).fillMaxWidth()) {
                    BoxWithConstraints(
                        Modifier.weight(1f).fillMaxHeight(),
                        contentAlignment = Alignment.Center,
                    ) {
                        val boardModifier = if (this.maxWidth.value / this.maxHeight.value > 1000f / 650f) {
                            Modifier.fillMaxHeight()
                        } else {
                            Modifier.fillMaxWidth()
                        }
                        BoardView(game, uiState.selected, viewModel::tapTerritory, boardModifier)
                    }
                    GameConsole(
                        game,
                        viewModel,
                        onCloseRemote,
                        { confirmingResignation = true },
                        Modifier.width(consoleWidth).fillMaxHeight(),
                    )
                }
            } else {
                BoardView(game, uiState.selected, viewModel::tapTerritory, Modifier.fillMaxWidth())
                GameConsole(game, viewModel, onCloseRemote, { confirmingResignation = true }, Modifier.weight(1f))
            }
        }
    }
    if (confirmingResignation) {
        AlertDialog(
            onDismissRequest = { confirmingResignation = false },
            title = { Text("RESIGN THIS GAME?") },
            text = { Text("Other players will be informed that you resigned.") },
            confirmButton = {
                TextButton(onClick = { confirmingResignation = false; onResignRemote() }) { Text("RESIGN GAME") }
            },
            dismissButton = {
                TextButton(onClick = { confirmingResignation = false }) { Text("CANCEL") }
            },
        )
    }
}

@Composable
private fun StatusBar(game: GameState) {
    Row(
        Modifier.fillMaxWidth().background(Color.Black).padding(horizontal = 8.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(if (game.turn == 0) "SETUP" else "TURN ${game.turn}", color = Color.White, fontSize = 10.sp)
        Spacer(Modifier.weight(1f))
        Text(game.players[game.currentPlayer].name, color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Bold)
        Text("  CARDS ${game.players[game.currentPlayer].cards.size}", color = Color.White, fontSize = 9.sp)
        Spacer(Modifier.weight(1f))
        Text(game.phase.label, color = Color.White, fontSize = 9.sp)
        if (game.phase == TurnPhase.REINFORCE) Text(" [${game.reinforcements}]", color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun GameConsole(
    game: GameState,
    viewModel: GameViewModel,
    onCloseRemote: () -> Unit,
    onResignRemote: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier.background(Color.White).border(0.dp, Color.Black).padding(9.dp),
        verticalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            Row(Modifier.fillMaxWidth()) {
                Column {
                    Text(if (game.turn == 0) "SETUP" else "TURN ${game.turn}", fontWeight = FontWeight.Bold)
                    Text(game.phase.label, fontSize = 10.sp)
                }
                Spacer(Modifier.weight(1f))
                Column(horizontalAlignment = Alignment.End) {
                    Text(game.players[game.currentPlayer].name, fontWeight = FontWeight.Bold)
                    when (game.phase) {
                        TurnPhase.CLAIM, TurnPhase.INITIAL_DEPLOY -> Text("${game.setupArmies[game.currentPlayer]} TO PLACE", fontSize = 10.sp)
                        TurnPhase.REINFORCE -> Text("${game.reinforcements} TO DEPLOY", fontSize = 10.sp)
                        else -> Unit
                    }
                }
            }
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                game.players.forEach { player ->
                    val territories = game.territories.values.count { it.owner == player.id }
                    val armies = game.territories.values.filter { it.owner == player.id }.sumOf { it.armies }
                    Column(
                        Modifier.size(width = 145.dp, height = 72.dp)
                            .background(if (player.id == game.currentPlayer) MediumGray else Color.White)
                            .border(if (player.id == game.currentPlayer) 3.dp else 1.dp, Color.Black)
                            .padding(6.dp),
                    ) {
                        Text((if (player.id == game.currentPlayer) "▶ " else "") + player.name, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                        Text(player.kind.label.uppercase(), fontSize = 8.sp)
                        Text("LAND $territories  ARMY $armies", fontSize = 9.sp)
                        Text("CARDS ${player.cards.size}  ${if (player.isAlive) "ACTIVE" else "OUT"}", fontSize = 9.sp)
                    }
                }
            }
            val localOwner = viewModel.uiState.localPlayerIndex.coerceIn(game.players.indices)
            Row(
                Modifier.fillMaxWidth().background(PaperGray).border(2.dp, Color.Black).padding(7.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                TerritoryPatternSwatch(localOwner, Modifier.size(38.dp))
                Column {
                    Text("YOUR TERRITORY PATTERN", fontSize = 10.sp, fontWeight = FontWeight.Bold)
                    Text(game.players[localOwner].name, fontSize = 9.sp)
                }
            }
            Text(game.message, Modifier.fillMaxWidth(), fontSize = 11.sp, minLines = 2, maxLines = 2)
        }
        if (viewModel.uiState.isRemote) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(9.dp)) {
                MacButton("GAMES", onCloseRemote, Modifier.weight(1f))
                MacButton("RESIGN", onResignRemote, Modifier.weight(1f))
            }
        }
        if (game.phase == TurnPhase.CLAIM || game.phase == TurnPhase.INITIAL_DEPLOY) {
            MacButton(
                "RANDOMIZE ALL ARMIES",
                viewModel::randomizeSetup,
                Modifier.fillMaxWidth(),
                enabled = viewModel.uiState.isLocalTurn,
            )
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(9.dp), verticalAlignment = Alignment.CenterVertically) {
            if (!viewModel.uiState.isRemote) MacButton("QUIT", viewModel::quitGame, Modifier.weight(.28f))
            when {
                viewModel.uiState.isRemote && !viewModel.uiState.isLocalTurn -> Instruction("WAITING FOR ${game.players[game.currentPlayer].name}", Modifier.weight(.72f))
                game.phase == TurnPhase.CLAIM -> Instruction("TAP AN UNCLAIMED LAND", Modifier.weight(.72f))
                game.phase == TurnPhase.INITIAL_DEPLOY -> Instruction("TAP YOUR LAND TO PLACE 1", Modifier.weight(.72f))
                game.phase == TurnPhase.REINFORCE && viewModel.canTradeCards() -> MacButton("TRADE +${viewModel.nextTradeValue()}", viewModel::tradeCards, Modifier.weight(.72f))
                game.phase == TurnPhase.REINFORCE -> Instruction("DEPLOY ${game.reinforcements} ARMIES", Modifier.weight(.72f))
                game.phase == TurnPhase.ATTACK -> MacButton("END ATTACKS", viewModel::nextPhase, Modifier.weight(.72f))
                game.phase == TurnPhase.FORTIFY -> MacButton("END TURN", viewModel::nextPhase, Modifier.weight(.72f))
                game.phase == TurnPhase.GAME_OVER -> Instruction("CAMPAIGN COMPLETE", Modifier.weight(.72f))
            }
        }
    }
}

@Composable
private fun Instruction(text: String, modifier: Modifier = Modifier) {
    Box(modifier.height(38.dp), contentAlignment = Alignment.Center) {
        Text(text, fontSize = 9.sp, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
    }
}
