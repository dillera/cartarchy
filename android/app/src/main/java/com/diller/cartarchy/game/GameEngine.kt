package com.diller.cartarchy.game

import com.diller.cartarchy.model.ClassicMap
import com.diller.cartarchy.model.ClassicRules
import com.diller.cartarchy.model.GameState
import com.diller.cartarchy.model.Player
import com.diller.cartarchy.model.PlayerKind
import com.diller.cartarchy.model.TerritoryState
import com.diller.cartarchy.model.TurnPhase
import kotlin.random.Random

class GameEngine(private val random: Random = Random.Default) {
    var game: GameState? = null
        private set
    var selected: String? = null
        private set
    var target: String? = null
        private set

    fun restore(saved: GameState) {
        val preservedSelection = selected?.takeIf { candidate ->
            game == saved && saved.phase in setOf(TurnPhase.ATTACK, TurnPhase.FORTIFY) &&
                saved.territories[candidate]?.let { territory ->
                    territory.owner == saved.currentPlayer && territory.armies > 1
                } == true
        }
        game = saved.deepCopy()
        selected = preservedSelection
        target = null
    }

    fun newGame(aiKinds: List<PlayerKind>) {
        val selection = aiKinds.take(5).ifEmpty { listOf(PlayerKind.BALANCED) }
        val players = mutableListOf(Player(0, "COMMANDER", PlayerKind.HUMAN))
        selection.forEachIndexed { index, kind -> players += Player(index + 1, "CPU ${index + 1}", kind) }
        startGame(players)
    }

    fun newRemoteGame(playerNames: List<String>) {
        require(playerNames.size in 2..6) { "Remote campaigns require 2 to 6 players" }
        startGame(playerNames.mapIndexed { index, name ->
            Player(index, name.uppercase(), PlayerKind.HUMAN)
        }.toMutableList())
    }

    private fun startGame(players: MutableList<Player>) {
        val territories = ClassicMap.territories.associate { it.id to TerritoryState(-1, 0) }.toMutableMap()
        val initial = ClassicRules.initialArmyCount(players.size)
        game = GameState(
            players = players,
            territories = territories,
            currentPlayer = 0,
            phase = TurnPhase.CLAIM,
            reinforcements = 0,
            turn = 0,
            message = "Claim one unoccupied territory.",
            deck = ClassicRules.deck(random),
            discard = mutableListOf(),
            tradeCount = 0,
            conqueredThisTurn = false,
            setupArmies = MutableList(players.size) { initial },
        )
        selected = null
        target = null
    }

    fun tap(id: String): Boolean {
        val current = game ?: return false
        if (current.players[current.currentPlayer].kind != PlayerKind.HUMAN) return false
        val territory = current.territories[id] ?: return false
        var changed = true
        when (current.phase) {
            TurnPhase.CLAIM -> {
                if (territory.owner != -1) return false
                current.territories[id] = TerritoryState(current.currentPlayer, 1)
                current.setupArmies[current.currentPlayer]--
                advanceSetupTurn(current)
            }
            TurnPhase.INITIAL_DEPLOY -> {
                if (territory.owner != current.currentPlayer || current.setupArmies[current.currentPlayer] <= 0) return false
                territory.armies++
                current.setupArmies[current.currentPlayer]--
                advanceSetupTurn(current)
            }
            TurnPhase.REINFORCE -> {
                if (current.players[current.currentPlayer].cards.size >= 5 && canTradeCards()) {
                    current.message = "You hold five cards. Trade a set before deploying."
                } else {
                    if (territory.owner != current.currentPlayer || current.reinforcements <= 0) return false
                    territory.armies++
                    current.reinforcements--
                    current.message = if (current.reinforcements == 0) {
                        current.phase = TurnPhase.ATTACK
                        "Reinforcement complete. Begin attacks."
                    } else {
                        "${current.reinforcements} armies remain."
                    }
                }
            }
            TurnPhase.ATTACK -> {
                if (territory.owner == current.currentPlayer && territory.armies > 1) {
                    selected = id
                    target = null
                    current.message = "Choose an adjacent enemy."
                } else {
                    val source = selected
                    if (source == null || id !in ClassicMap.byId.getValue(source).neighbors || territory.owner == current.currentPlayer) return false
                    target = id
                    attack(source, id, current)
                }
            }
            TurnPhase.FORTIFY -> {
                if (selected == null && territory.owner == current.currentPlayer && territory.armies > 1) {
                    selected = id
                    current.message = "Choose an adjacent territory you own."
                } else {
                    val source = selected
                    if (source == null || territory.owner != current.currentPlayer || id !in ClassicMap.byId.getValue(source).neighbors) return false
                    val moving = current.territories.getValue(source).armies - 1
                    current.territories.getValue(source).armies = 1
                    territory.armies += moving
                    endTurn(current)
                }
            }
            TurnPhase.GAME_OVER -> changed = false
        }
        continueAiIfNeeded()
        return changed
    }

    fun nextPhase(): Boolean {
        val current = game ?: return false
        if (current.players[current.currentPlayer].kind != PlayerKind.HUMAN) return false
        when (current.phase) {
            TurnPhase.ATTACK -> {
                current.phase = TurnPhase.FORTIFY
                selected = null
                current.message = "Move armies once, or end your turn."
            }
            TurnPhase.FORTIFY -> endTurn(current)
            else -> return false
        }
        continueAiIfNeeded()
        return true
    }

    fun canTradeCards(): Boolean {
        val current = game ?: return false
        return current.phase == TurnPhase.REINFORCE &&
            ClassicRules.validSetIndices(current.players[current.currentPlayer].cards) != null
    }

    fun nextTradeValue(): Int = game?.let { ClassicRules.tradeValue(it.tradeCount) } ?: 4

    fun tradeCards(): Boolean {
        val current = game ?: return false
        if (current.players[current.currentPlayer].kind != PlayerKind.HUMAN || !canTradeCards()) return false
        performTrade(current)
        current.message = "Card set traded. Deploy ${current.reinforcements} armies."
        return true
    }

    fun randomizeSetup(): Boolean {
        val current = game ?: return false
        if (current.phase != TurnPhase.CLAIM && current.phase != TurnPhase.INITIAL_DEPLOY) return false
        val territoryIds = ClassicMap.territories.map { it.id }.shuffled(random)
        current.territories.values.forEach { territory ->
            territory.owner = -1
            territory.armies = 0
        }
        territoryIds.forEachIndexed { index, id ->
            current.territories[id] = TerritoryState(owner = index % current.players.size, armies = 1)
        }
        val startingArmies = ClassicRules.initialArmyCount(current.players.size)
        current.players.indices.forEach { owner ->
            val owned = territoryIds.filter { current.territories.getValue(it).owner == owner }
            repeat(startingArmies - owned.size) {
                current.territories.getValue(owned[random.nextInt(owned.size)]).armies++
            }
        }
        current.setupArmies.indices.forEach { current.setupArmies[it] = 0 }
        current.currentPlayer = 0
        current.phase = TurnPhase.REINFORCE
        current.turn = 1
        current.reinforcements = ClassicRules.reinforcementCount(0, current.territories)
        current.conqueredThisTurn = false
        current.message = "Armies placed randomly. ${current.players[0].name} reinforces."
        selected = null
        target = null
        return true
    }

    private fun attack(source: String, destination: String, current: GameState) {
        val sourceState = current.territories.getValue(source)
        val destinationState = current.territories.getValue(destination)
        val attackDice = minOf(3, sourceState.armies - 1)
        val defendDice = minOf(2, destinationState.armies)
        val attackRolls = List(attackDice) { random.nextInt(1, 7) }.sortedDescending()
        val defendRolls = List(defendDice) { random.nextInt(1, 7) }.sortedDescending()
        var attackerLoss = 0
        var defenderLoss = 0
        repeat(minOf(attackRolls.size, defendRolls.size)) { index ->
            if (attackRolls[index] > defendRolls[index]) defenderLoss++ else attackerLoss++
        }
        sourceState.armies -= attackerLoss
        destinationState.armies -= defenderLoss
        current.message = "Rolled ${attackRolls.joinToString(",")} vs ${defendRolls.joinToString(",")}."
        if (destinationState.armies == 0) {
            val moving = minOf(attackDice, sourceState.armies - 1)
            sourceState.armies -= moving
            current.territories[destination] = TerritoryState(current.currentPlayer, moving)
            current.conqueredThisTurn = true
            current.message += " Territory captured!"
            eliminateAndCheckVictory(current)
        }
    }

    private fun endTurn(current: GameState) {
        selected = null
        target = null
        if (current.conqueredThisTurn) {
            if (current.deck.isEmpty()) {
                current.deck += current.discard.shuffled(random)
                current.discard.clear()
            }
            if (current.deck.isNotEmpty()) {
                current.players[current.currentPlayer].cards += current.deck.removeAt(current.deck.lastIndex)
            }
        }
        do {
            current.currentPlayer = (current.currentPlayer + 1) % current.players.size
        } while (!current.players[current.currentPlayer].isAlive)
        if (current.currentPlayer == 0) current.turn++
        current.phase = TurnPhase.REINFORCE
        current.reinforcements = ClassicRules.reinforcementCount(current.currentPlayer, current.territories)
        current.conqueredThisTurn = false
        current.message = "${current.players[current.currentPlayer].name} reinforces."
    }

    private fun continueAiIfNeeded() {
        val current = game ?: return
        var safety = 0
        while (current.phase != TurnPhase.GAME_OVER && current.players[current.currentPlayer].kind != PlayerKind.HUMAN && safety < 500) {
            safety++
            val owner = current.currentPlayer
            when (current.phase) {
                TurnPhase.CLAIM -> {
                    current.territories.filterValues { it.owner == -1 }.keys.randomOrNull(random)?.let { id ->
                        current.territories[id] = TerritoryState(owner, 1)
                        current.setupArmies[owner]--
                    }
                    advanceSetupTurn(current)
                }
                TurnPhase.INITIAL_DEPLOY -> {
                    val owned = ownedTerritories(current, owner)
                    chooseReinforcementTerritory(current, owner, owned)?.let { id ->
                        current.territories.getValue(id).armies++
                        current.setupArmies[owner]--
                    }
                    advanceSetupTurn(current)
                }
                TurnPhase.REINFORCE -> {
                    while (ClassicRules.validSetIndices(current.players[owner].cards) != null) performTrade(current)
                    val owned = ownedTerritories(current, owner)
                    while (current.reinforcements > 0) {
                        chooseReinforcementTerritory(current, owner, owned)?.let { current.territories.getValue(it).armies++ }
                        current.reinforcements--
                    }
                    current.phase = TurnPhase.ATTACK
                }
                TurnPhase.ATTACK -> {
                    val move = chooseAiAttack(current, owner)
                    if (move != null) attack(move.first, move.second, current) else {
                        current.phase = TurnPhase.FORTIFY
                        endTurn(current)
                    }
                }
                TurnPhase.FORTIFY -> endTurn(current)
                TurnPhase.GAME_OVER -> Unit
            }
        }
        if (safety == 500 && current.phase != TurnPhase.GAME_OVER) current.message = "AI safety limit reached."
    }

    private fun chooseReinforcementTerritory(current: GameState, owner: Int, owned: List<String>): String? {
        val borders = owned.filter { id -> ClassicMap.byId.getValue(id).neighbors.any { current.territories.getValue(it).owner != owner } }
        val choices = borders.ifEmpty { owned }
        return when (current.players[owner].kind) {
            PlayerKind.CAUTIOUS -> choices.minByOrNull { current.territories.getValue(it).armies }
            PlayerKind.AGGRESSIVE -> choices.maxByOrNull { current.territories.getValue(it).armies }
            else -> choices.maxByOrNull { id ->
                ClassicMap.byId.getValue(id).neighbors
                    .filter { current.territories.getValue(it).owner != owner }
                    .maxOfOrNull { current.territories.getValue(it).armies } ?: 0
            }
        }
    }

    private fun chooseAiAttack(current: GameState, owner: Int): Pair<String, String>? {
        val threshold = when (current.players[owner].kind) {
            PlayerKind.CAUTIOUS -> 3
            PlayerKind.BALANCED -> 1
            PlayerKind.AGGRESSIVE -> 0
            PlayerKind.HUMAN -> Int.MAX_VALUE
        }
        return ownedTerritories(current, owner).flatMap { source ->
            ClassicMap.byId.getValue(source).neighbors
                .filter { current.territories.getValue(it).owner != owner }
                .map { destination -> source to destination }
        }.filter { (source, destination) ->
            current.territories.getValue(source).armies > 1 &&
                current.territories.getValue(source).armies >= current.territories.getValue(destination).armies + threshold
        }.maxByOrNull { (source, destination) ->
            current.territories.getValue(source).armies - current.territories.getValue(destination).armies
        }
    }

    private fun advanceSetupTurn(current: GameState) {
        if (current.phase == TurnPhase.CLAIM && current.territories.values.none { it.owner == -1 }) {
            current.phase = TurnPhase.INITIAL_DEPLOY
        }
        if (current.phase == TurnPhase.INITIAL_DEPLOY && current.setupArmies.all { it == 0 }) {
            current.currentPlayer = 0
            current.phase = TurnPhase.REINFORCE
            current.turn = 1
            current.reinforcements = ClassicRules.reinforcementCount(0, current.territories)
            current.message = "Setup complete. Deploy reinforcements."
            return
        }
        do {
            current.currentPlayer = (current.currentPlayer + 1) % current.players.size
        } while (current.phase == TurnPhase.INITIAL_DEPLOY && current.setupArmies[current.currentPlayer] == 0)
        val remaining = current.setupArmies[current.currentPlayer]
        current.message = if (current.phase == TurnPhase.CLAIM) {
            "${current.players[current.currentPlayer].name} claims a territory."
        } else {
            "${current.players[current.currentPlayer].name} places an army. $remaining remain."
        }
    }

    private fun performTrade(current: GameState) {
        val owner = current.currentPlayer
        val indices = ClassicRules.validSetIndices(current.players[owner].cards) ?: return
        val cards = indices.map { current.players[owner].cards[it] }
        current.reinforcements += ClassicRules.tradeValue(current.tradeCount)
        current.tradeCount++
        cards.mapNotNull { it.territoryId }.firstOrNull { current.territories[it]?.owner == owner }?.let { territory ->
            current.territories.getValue(territory).armies += 2
        }
        indices.sortedDescending().forEach { current.players[owner].cards.removeAt(it) }
        current.discard += cards
    }

    private fun eliminateAndCheckVictory(current: GameState) {
        current.players.indices
            .filter { current.players[it].isAlive && current.territories.values.none { territory -> territory.owner == it } }
            .forEach { playerIndex ->
                if (playerIndex != current.currentPlayer) {
                    current.players[current.currentPlayer].cards += current.players[playerIndex].cards
                    current.players[playerIndex].cards.clear()
                }
                current.players[playerIndex].isAlive = false
            }
        val alive = current.players.filter { it.isAlive }
        if (alive.size == 1) {
            current.phase = TurnPhase.GAME_OVER
            current.message = "${alive.first().name} conquers the world."
        }
    }

    private fun ownedTerritories(current: GameState, owner: Int) =
        current.territories.filterValues { it.owner == owner }.keys.toList()
}

private fun <T> Collection<T>.randomOrNull(random: Random): T? = if (isEmpty()) null else elementAt(random.nextInt(size))
