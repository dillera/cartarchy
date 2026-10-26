package com.diller.cartarchy.server

import com.diller.cartarchy.game.GameCodec
import com.diller.cartarchy.model.PlayerKind
import com.diller.cartarchy.model.TurnPhase
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ServerGameCodecTest {
    @Test
    fun serverGameRetainsRosterMembershipAndRemoteState() {
        val game = serverGameFromJson(JSONObject(SERVER_GAME_JSON))

        assertEquals(listOf("janus-id", "castor-id", "pollux-id"), game.playerIds)
        assertEquals(listOf("Janus", "Castor", "Pollux"), game.players.map { it.name })
        assertEquals("janus-id", game.currentPlayerId)
        assertEquals("PLAY Real 2", game.playLabel)
        assertTrue(game.isMember("janus-id"))
    }

    @Test
    fun androidDecodesAndEncodesTheSharedIosServerStateFormat() {
        val stateJson = JSONObject(IOS_STATE_JSON)
        val game = GameCodec.decodeServer(stateJson)

        assertEquals(TurnPhase.CLAIM, game.phase)
        assertEquals(PlayerKind.HUMAN, game.players.first().kind)
        assertEquals("alaska", game.deck.first().territoryId)

        val encoded = GameCodec.encodeServer(game)
        assertEquals("CLAIM TERRITORIES", encoded.getString("phase"))
        assertEquals("Human", encoded.getJSONArray("players").getJSONObject(0).getString("kind"))
        assertEquals("alaska", encoded.getJSONArray("deck").getJSONObject(0).getString("territoryID"))
        assertEquals("infantry", encoded.getJSONArray("deck").getJSONObject(0).getString("unit"))
    }

    private companion object {
        const val SERVER_GAME_JSON = """
            {
              "id":"real-2","name":"Real 2","mode":"turn-based","status":"active",
              "hostId":"janus-id","playerIds":["janus-id","castor-id","pollux-id"],
              "players":[{"id":"janus-id","name":"Janus"},{"id":"castor-id","name":"Castor"},{"id":"pollux-id","name":"Pollux"}],
              "currentPlayerId":"janus-id","maxPlayers":3,"currentRound":1,"currentTurn":1,"version":4
            }
        """

        const val IOS_STATE_JSON = """
            {
              "players":[{"id":0,"name":"JANUS","kind":"Human","isAlive":true,"cards":[]}],
              "territories":{"alaska":{"owner":-1,"armies":0}},
              "currentPlayer":0,"phase":"CLAIM TERRITORIES","reinforcements":0,"turn":0,
              "message":"Claim one unoccupied territory.",
              "deck":[{"id":"alaska","territoryID":"alaska","unit":"infantry"}],
              "discard":[],"tradeCount":0,"conqueredThisTurn":false,"setupArmies":[35]
            }
        """
    }
}
