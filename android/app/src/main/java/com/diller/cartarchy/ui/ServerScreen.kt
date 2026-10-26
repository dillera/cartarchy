package com.diller.cartarchy.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.diller.cartarchy.server.ServerGame
import com.diller.cartarchy.server.ServerViewModel
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning

@Composable
fun ServerScreen(viewModel: ServerViewModel, onDone: () -> Unit) {
    val state = viewModel.uiState
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val scanner = remember {
        val options = GmsBarcodeScannerOptions.Builder()
            .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
            .enableAutoZoom()
            .build()
        GmsBarcodeScanning.getClient(context, options)
    }

    Column(
        Modifier.fillMaxSize().background(PaperGray).statusBarsPadding().navigationBarsPadding().padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("GAME SERVER", Modifier.weight(1f), fontSize = 22.sp, fontWeight = FontWeight.Bold)
            MacButton("DONE", onDone, Modifier.weight(.34f))
        }
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            OutlinedTextField(
                value = state.serverUrl,
                onValueChange = viewModel::updateServerUrl,
                label = { Text("SERVER URL") },
                placeholder = { Text("http://server:8080") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None),
            )
            OutlinedTextField(
                value = state.pairingCode,
                onValueChange = viewModel::updatePairingCode,
                label = { Text("PAIRING CODE") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None),
            )
            OutlinedTextField(
                value = state.playerName,
                onValueChange = viewModel::updatePlayerName,
                label = { Text("PLAYER NAME") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
            MacButton("SCAN SERVER OR GAME QR", {
                scanner.startScan()
                    .addOnSuccessListener { barcode -> barcode.rawValue?.let(viewModel::accept) }
                    .addOnFailureListener { error -> viewModel.reportScannerError(error.message ?: "SCAN FAILED") }
            })
            MacButton("CONNECT", viewModel::register, enabled = !state.busy)
            if (state.isConnected) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    MacButton("REFRESH", viewModel::refreshGames, Modifier.weight(1f), enabled = !state.busy)
                    MacButton("DISCONNECT", viewModel::disconnect, Modifier.weight(1f), enabled = !state.busy)
                }
                if (state.profileUrl.isNotBlank()) {
                    MacButton("OPEN COMMANDER PROFILE", { uriHandler.openUri(state.profileUrl) })
                }
                Spacer(Modifier.height(4.dp))
                Text("YOUR GAMES", fontWeight = FontWeight.Bold)
                if (state.games.isEmpty()) {
                    Text("No joined games. Scan a game QR to join.", style = MaterialTheme.typography.bodySmall)
                } else {
                    state.games.forEach { ServerGameCard(it) }
                }
            }
        }
        Text(
            state.status,
            Modifier.fillMaxWidth().background(Color.Black).padding(9.dp),
            color = Color.White,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun ServerGameCard(game: ServerGame) {
    Column(
        Modifier.fillMaxWidth().background(Color.White).border(2.dp, Color.Black).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        Row(Modifier.fillMaxWidth()) {
            Text(game.name.uppercase(), Modifier.weight(1f), fontWeight = FontWeight.Bold)
            Text(game.status.uppercase(), fontWeight = FontWeight.Bold, fontSize = 11.sp)
        }
        Text("${game.mode.uppercase()}  •  ${game.players.size}/${game.maxPlayers} PLAYERS", fontSize = 10.sp)
        if (game.players.isNotEmpty()) Text(game.players.joinToString("  •  ") { it.name.uppercase() }, fontSize = 10.sp)
        if (game.currentRound > 0) Text("ROUND ${game.currentRound}", fontSize = 10.sp)
    }
}
