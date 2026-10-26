package com.diller.cartarchy.model

import kotlin.random.Random

object ClassicRules {
    fun initialArmyCount(players: Int): Int = when (players) {
        2 -> 40
        3 -> 35
        4 -> 30
        5 -> 25
        else -> 20
    }

    fun reinforcementCount(owner: Int, states: Map<String, TerritoryState>): Int {
        var total = maxOf(3, states.values.count { it.owner == owner } / 3)
        Continent.entries.forEach { continent ->
            val ids = ClassicMap.territories.filter { it.continent == continent }.map { it.id }
            if (ids.all { states[it]?.owner == owner }) total += continent.bonus
        }
        return total
    }

    fun tradeValue(number: Int): Int {
        val opening = listOf(4, 6, 8, 10, 12, 15)
        return opening.getOrNull(number) ?: (15 + (number - opening.size + 1) * 5)
    }

    fun validSetIndices(cards: List<CartarchyCard>): List<Int>? {
        if (cards.size < 3) return null
        for (a in 0 until cards.size - 2) {
            for (b in a + 1 until cards.size - 1) {
                for (c in b + 1 until cards.size) {
                    if (isValidSet(listOf(cards[a], cards[b], cards[c]))) return listOf(a, b, c)
                }
            }
        }
        return null
    }

    fun isValidSet(cards: List<CartarchyCard>): Boolean {
        if (cards.size != 3) return false
        val nonWild = cards.mapNotNull { it.unit }
        return nonWild.size < 3 || nonWild.toSet().size == 1 || nonWild.toSet().size == 3
    }

    fun deck(random: Random = Random.Default): MutableList<CartarchyCard> {
        val units = CardUnit.entries
        val cards = ClassicMap.territories.mapIndexed { index, territory ->
            CartarchyCard(territory.id, territory.id, units[index % units.size])
        }.toMutableList()
        cards += CartarchyCard("wild-1", null, null)
        cards += CartarchyCard("wild-2", null, null)
        cards.shuffle(random)
        return cards
    }
}
