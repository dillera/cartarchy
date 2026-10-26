package com.diller.cartarchy.game

import com.diller.cartarchy.model.ClassicMap
import com.diller.cartarchy.model.PlayerKind
import com.diller.cartarchy.model.TurnPhase
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class GameEngineTest {
    @Test
    fun newCampaignDoesNotAutoDeployAnyArmies() {
        val engine = GameEngine(Random(1))
        engine.newGame(listOf(PlayerKind.CAUTIOUS, PlayerKind.BALANCED, PlayerKind.AGGRESSIVE))
        val game = engine.game!!
        assertEquals(TurnPhase.CLAIM, game.phase)
        assertTrue(game.territories.values.all { it.owner == -1 && it.armies == 0 })
        assertEquals(listOf(30, 30, 30, 30), game.setupArmies)
    }

    @Test
    fun aHumanClaimAdvancesOneRoundAcrossAiPlayers() {
        val engine = GameEngine(Random(2))
        engine.newGame(listOf(PlayerKind.CAUTIOUS, PlayerKind.BALANCED, PlayerKind.AGGRESSIVE))
        assertTrue(engine.tap("alaska"))
        val game = engine.game!!
        assertEquals(0, game.currentPlayer)
        assertEquals(4, game.territories.values.count { it.owner >= 0 })
        assertEquals(0, game.territories.getValue("alaska").owner)
        assertEquals(29, game.setupArmies[0])
    }

    @Test
    fun setupCompletesOnlyAfterEveryArmyIsPlacedInRounds() {
        val engine = GameEngine(Random(3))
        engine.newGame(listOf(PlayerKind.BALANCED))
        var guard = 0
        while (engine.game!!.phase == TurnPhase.CLAIM && guard++ < 100) {
            val unclaimed = ClassicMap.territories.first { engine.game!!.territories.getValue(it.id).owner == -1 }
            engine.tap(unclaimed.id)
        }
        assertEquals(TurnPhase.INITIAL_DEPLOY, engine.game!!.phase)
        assertTrue(engine.game!!.setupArmies.any { it > 0 })
        while (engine.game!!.phase == TurnPhase.INITIAL_DEPLOY && guard++ < 300) {
            val owned = ClassicMap.territories.first { engine.game!!.territories.getValue(it.id).owner == 0 }
            engine.tap(owned.id)
        }
        assertTrue("setup loop reached its guard", guard < 300)
        assertEquals(TurnPhase.REINFORCE, engine.game!!.phase)
        assertEquals(1, engine.game!!.turn)
        assertTrue(engine.game!!.setupArmies.all { it == 0 })
    }

    @Test
    fun gameSaveCodecRoundTripsCompleteState() {
        val engine = GameEngine(Random(4))
        engine.newGame(listOf(PlayerKind.CAUTIOUS, PlayerKind.AGGRESSIVE))
        engine.tap("alaska")
        val original = engine.game!!
        val restored = GameCodec.decode(GameCodec.encode(original))
        assertEquals(original, restored)
        assertNotNull(restored.territories["alaska"])
    }

    @Test
    fun invalidTerritoryTapDoesNotChangeGame() {
        val engine = GameEngine(Random(5))
        engine.newGame(listOf(PlayerKind.BALANCED))
        val before = engine.game!!.deepCopy()
        assertFalse(engine.tap("not-a-territory"))
        assertEquals(before, engine.game)
    }

    @Test
    fun remoteCampaignUsesHumanPlayersAndAdvancesOnlyOneSeatPerAction() {
        val engine = GameEngine(Random(6))
        engine.newRemoteGame(listOf("Janus", "Castor", "Pollux"))

        assertTrue(engine.game!!.players.all { it.kind == PlayerKind.HUMAN })
        assertTrue(engine.tap("alaska"))
        assertEquals(1, engine.game!!.currentPlayer)
        assertEquals(1, engine.game!!.territories.values.count { it.owner >= 0 })
    }

    @Test
    fun remoteAttackCanChooseTargetAfterServerEchoesSourceSelection() {
        val engine = GameEngine(Random(7))
        engine.newRemoteGame(listOf("Janus", "Castor"))
        engine.game!!.apply {
            currentPlayer = 0
            phase = TurnPhase.ATTACK
            territories.getValue("alaska").apply { owner = 0; armies = 4 }
            territories.getValue("nwTerritory").apply { owner = 1; armies = 2 }
        }

        assertTrue(engine.tap("alaska"))
        assertEquals("alaska", engine.selected)
        val echoedByServer = engine.game!!.deepCopy()
        engine.restore(echoedByServer)

        assertTrue("adjacent enemy target was rejected after server echo", engine.tap("nwTerritory"))
        assertEquals("nwTerritory", engine.target)
    }

    @Test
    fun randomSetupPlacesEveryStartingArmyAndBeginsFirstTurn() {
        val engine = GameEngine(Random(8))
        engine.newGame(listOf(PlayerKind.CAUTIOUS, PlayerKind.BALANCED, PlayerKind.AGGRESSIVE))
        engine.tap("alaska")

        assertTrue(engine.randomizeSetup())
        val game = engine.game!!
        assertEquals(TurnPhase.REINFORCE, game.phase)
        assertEquals(0, game.currentPlayer)
        assertEquals(1, game.turn)
        assertTrue(game.setupArmies.all { it == 0 })
        assertTrue(game.territories.values.all { it.owner in game.players.indices && it.armies >= 1 })
        game.players.indices.forEach { owner ->
            assertEquals(30, game.territories.values.filter { it.owner == owner }.sumOf { it.armies })
        }
        val territoryCounts = game.players.indices.map { owner -> game.territories.values.count { it.owner == owner } }
        assertTrue(territoryCounts.max() - territoryCounts.min() <= 1)
        assertFalse(engine.randomizeSetup())
    }
}
