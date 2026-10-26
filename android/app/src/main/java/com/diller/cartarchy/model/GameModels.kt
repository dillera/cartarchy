package com.diller.cartarchy.model

enum class Continent(val label: String, val bonus: Int) {
    NORTH_AMERICA("N. America", 5),
    SOUTH_AMERICA("S. America", 2),
    EUROPE("Europe", 5),
    AFRICA("Africa", 3),
    ASIA("Asia", 7),
    AUSTRALIA("Australia", 2),
}

data class TerritoryDefinition(
    val id: String,
    val name: String,
    val continent: Continent,
    val x: Double,
    val y: Double,
    val neighbors: List<String>,
)

enum class PlayerKind(val label: String) {
    HUMAN("Human"),
    CAUTIOUS("Cautious AI"),
    BALANCED("Balanced AI"),
    AGGRESSIVE("Aggressive AI"),
}

enum class CardUnit { INFANTRY, CAVALRY, ARTILLERY }

data class CartarchyCard(
    val id: String,
    val territoryId: String?,
    val unit: CardUnit?,
)

data class Player(
    val id: Int,
    var name: String,
    var kind: PlayerKind,
    var isAlive: Boolean = true,
    val cards: MutableList<CartarchyCard> = mutableListOf(),
)

data class TerritoryState(var owner: Int, var armies: Int)

enum class TurnPhase(val label: String) {
    CLAIM("CLAIM TERRITORIES"),
    INITIAL_DEPLOY("PLACE ARMIES"),
    REINFORCE("REINFORCE"),
    ATTACK("ATTACK"),
    FORTIFY("FORTIFY"),
    GAME_OVER("GAME OVER"),
}

data class GameState(
    val players: MutableList<Player>,
    val territories: MutableMap<String, TerritoryState>,
    var currentPlayer: Int,
    var phase: TurnPhase,
    var reinforcements: Int,
    var turn: Int,
    var message: String,
    val deck: MutableList<CartarchyCard>,
    val discard: MutableList<CartarchyCard>,
    var tradeCount: Int,
    var conqueredThisTurn: Boolean,
    val setupArmies: MutableList<Int>,
) {
    fun deepCopy(): GameState = GameState(
        players = players.map { it.copy(cards = it.cards.toMutableList()) }.toMutableList(),
        territories = territories.mapValues { (_, state) -> state.copy() }.toMutableMap(),
        currentPlayer = currentPlayer,
        phase = phase,
        reinforcements = reinforcements,
        turn = turn,
        message = message,
        deck = deck.toMutableList(),
        discard = discard.toMutableList(),
        tradeCount = tradeCount,
        conqueredThisTurn = conqueredThisTurn,
        setupArmies = setupArmies.toMutableList(),
    )
}
