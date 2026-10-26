package com.diller.cartarchy.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ClassicRulesTest {
    @Test
    fun progressiveCardTradeValuesMatchClassicRules() {
        assertEquals(listOf(4, 6, 8, 10, 12, 15, 20, 25, 30), (0..8).map(ClassicRules::tradeValue))
    }

    @Test
    fun initialArmiesFollowClassicPlayerCounts() {
        assertEquals(listOf(40, 35, 30, 25, 20), (2..6).map(ClassicRules::initialArmyCount))
    }

    @Test
    fun cardSetsAcceptMatchingMixedAndWildCombinations() {
        fun card(unit: CardUnit?) = CartarchyCard(java.util.UUID.randomUUID().toString(), null, unit)
        assertTrue(ClassicRules.isValidSet(listOf(card(CardUnit.INFANTRY), card(CardUnit.INFANTRY), card(CardUnit.INFANTRY))))
        assertTrue(ClassicRules.isValidSet(listOf(card(CardUnit.INFANTRY), card(CardUnit.CAVALRY), card(CardUnit.ARTILLERY))))
        assertTrue(ClassicRules.isValidSet(listOf(card(CardUnit.INFANTRY), card(CardUnit.INFANTRY), card(null))))
        assertFalse(ClassicRules.isValidSet(listOf(card(CardUnit.INFANTRY), card(CardUnit.INFANTRY), card(CardUnit.CAVALRY))))
    }

    @Test
    fun reinforcementsUseTerritoriesMinimumAndContinentBonuses() {
        val states = ClassicMap.territories.associate { it.id to TerritoryState(1, 1) }.toMutableMap()
        assertEquals(38, ClassicRules.reinforcementCount(1, states))
        ClassicMap.territories.filter { it.continent != Continent.AUSTRALIA }.forEach { states.getValue(it.id).owner = 0 }
        assertEquals(5, ClassicRules.reinforcementCount(1, states))
    }

    @Test
    fun deckContainsEveryTerritoryAndTwoWildCards() {
        val deck = ClassicRules.deck(kotlin.random.Random(7))
        assertEquals(44, deck.size)
        assertEquals(2, deck.count { it.unit == null })
        assertEquals(42, deck.mapNotNull { it.territoryId }.toSet().size)
    }
}
