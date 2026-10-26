package com.diller.cartarchy.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ClassicMapTest {
    @Test
    fun classicMapHasFortyTwoUniqueTerritories() {
        assertEquals(42, ClassicMap.territories.size)
        assertEquals(42, ClassicMap.byId.size)
    }

    @Test
    fun everyBorderIsSymmetricAndPointsToKnownTerritory() {
        ClassicMap.territories.forEach { territory ->
            territory.neighbors.forEach { neighborId ->
                assertTrue("Unknown neighbor $neighborId", neighborId in ClassicMap.byId)
                assertTrue("Border ${territory.id}–$neighborId is not symmetric", territory.id in ClassicMap.byId.getValue(neighborId).neighbors)
            }
        }
    }

    @Test
    fun classicCrossContinentLinksArePresent() {
        val expected = listOf(
            "alaska" to "kamchatka",
            "greenland" to "iceland",
            "brazil" to "northAfrica",
            "siam" to "indonesia",
        )
        expected.forEach { (from, to) -> assertTrue(to in ClassicMap.byId.getValue(from).neighbors) }
    }
}
