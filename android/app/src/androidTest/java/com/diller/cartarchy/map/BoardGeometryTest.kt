package com.diller.cartarchy.map

import androidx.compose.ui.geometry.Size
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.diller.cartarchy.model.ClassicMap
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class BoardGeometryTest {
    @Test
    fun copiedBoardAssetContainsAndHitTestsEveryTerritory() {
        val geometry = BoardGeometry.load(ApplicationProvider.getApplicationContext())
        assertEquals(ClassicMap.byId.keys, geometry.territoryIds)
        val board = geometry.createPaths(Size(393f, 393f / BoardGeometry.ASPECT_RATIO))
        ClassicMap.territories.forEach { territory ->
            val path = board.paths.getValue(territory.id)
            val center = board.centers.getValue(territory.id)
            assertFalse("Missing ${territory.id}", path.isEmpty)
            assertTrue("Marker outside ${territory.id}", path.containsPoint(center))
            assertEquals(territory.id, board.territoryAt(center))
        }
    }
}
