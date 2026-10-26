package com.diller.cartarchy.game

import com.diller.cartarchy.model.CardUnit
import com.diller.cartarchy.model.GameState
import com.diller.cartarchy.model.Player
import com.diller.cartarchy.model.PlayerKind
import com.diller.cartarchy.model.CartarchyCard
import com.diller.cartarchy.model.TerritoryState
import com.diller.cartarchy.model.TurnPhase
import org.json.JSONArray
import org.json.JSONObject

object GameCodec {
    fun encode(game: GameState): String = encodeJson(game, wireFormat = false).toString()

    fun decode(source: String): GameState {
        return decode(JSONObject(source), wireFormat = false)
    }

    fun encodeServer(game: GameState): JSONObject = encodeJson(game, wireFormat = true)

    fun decodeServer(json: JSONObject): GameState = decode(json, wireFormat = true)

    private fun encodeJson(game: GameState, wireFormat: Boolean): JSONObject = JSONObject().apply {
        put("players", JSONArray(game.players.map { playerToJson(it, wireFormat) }))
        put("territories", JSONObject().apply {
            game.territories.forEach { (id, state) -> put(id, JSONObject().put("owner", state.owner).put("armies", state.armies)) }
        })
        put("currentPlayer", game.currentPlayer)
        put("phase", if (wireFormat) game.phase.label else game.phase.name)
        put("reinforcements", game.reinforcements)
        put("turn", game.turn)
        put("message", game.message)
        put("deck", JSONArray(game.deck.map { cardToJson(it, wireFormat) }))
        put("discard", JSONArray(game.discard.map { cardToJson(it, wireFormat) }))
        put("tradeCount", game.tradeCount)
        put("conqueredThisTurn", game.conqueredThisTurn)
        put("setupArmies", JSONArray(game.setupArmies))
    }

    private fun decode(json: JSONObject, wireFormat: Boolean): GameState {
        val players = json.getJSONArray("players").objects().map { value ->
            val player = value as JSONObject
            Player(
                id = player.getInt("id"),
                name = player.getString("name"),
                kind = playerKind(player.getString("kind")),
                isAlive = player.getBoolean("isAlive"),
                cards = player.getJSONArray("cards").objects().map { cardFromJson(it as JSONObject, wireFormat) }.toMutableList(),
            )
        }.toMutableList()
        val territoriesJson = json.getJSONObject("territories")
        val territories = territoriesJson.keys().asSequence().associateWith { id ->
            val state = territoriesJson.getJSONObject(id)
            TerritoryState(state.getInt("owner"), state.getInt("armies"))
        }.toMutableMap()
        return GameState(
            players = players,
            territories = territories,
            currentPlayer = json.getInt("currentPlayer"),
            phase = turnPhase(json.getString("phase")),
            reinforcements = json.getInt("reinforcements"),
            turn = json.getInt("turn"),
            message = json.getString("message"),
            deck = json.getJSONArray("deck").objects().map { cardFromJson(it as JSONObject, wireFormat) }.toMutableList(),
            discard = json.getJSONArray("discard").objects().map { cardFromJson(it as JSONObject, wireFormat) }.toMutableList(),
            tradeCount = json.getInt("tradeCount"),
            conqueredThisTurn = json.getBoolean("conqueredThisTurn"),
            setupArmies = json.getJSONArray("setupArmies").objects().map { it as Int }.toMutableList(),
        )
    }

    private fun playerToJson(player: Player, wireFormat: Boolean = false) = JSONObject()
        .put("id", player.id)
        .put("name", player.name)
        .put("kind", if (wireFormat) player.kind.label else player.kind.name)
        .put("isAlive", player.isAlive)
        .put("cards", JSONArray(player.cards.map { cardToJson(it, wireFormat) }))

    private fun cardToJson(card: CartarchyCard, wireFormat: Boolean = false) = JSONObject()
        .put("id", card.id)
        .put(if (wireFormat) "territoryID" else "territoryId", card.territoryId ?: JSONObject.NULL)
        .put("unit", card.unit?.let { if (wireFormat) it.name.lowercase() else it.name } ?: JSONObject.NULL)

    private fun cardFromJson(json: JSONObject, wireFormat: Boolean) = CartarchyCard(
        id = json.getString("id"),
        territoryId = json.optNullableString(if (wireFormat) "territoryID" else "territoryId")
            ?: json.optNullableString(if (wireFormat) "territoryId" else "territoryID"),
        unit = json.optNullableString("unit")?.uppercase()?.let(CardUnit::valueOf),
    )

    private fun playerKind(value: String) = PlayerKind.entries.firstOrNull { it.name == value || it.label == value }
        ?: throw IllegalArgumentException("Unknown player kind: $value")

    private fun turnPhase(value: String) = TurnPhase.entries.firstOrNull { it.name == value || it.label == value }
        ?: throw IllegalArgumentException("Unknown turn phase: $value")
}

private fun JSONArray.objects(): Sequence<Any> = sequence { for (index in 0 until length()) yield(get(index)) }
private fun JSONObject.optNullableString(key: String): String? =
    if (!has(key) || isNull(key)) null else optString(key).takeIf(String::isNotEmpty)
