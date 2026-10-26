package com.diller.cartarchy.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AdaptiveLayoutPolicyTest {
    @Test
    fun phonesUseStackedPortraitAndSplitLandscapeLayouts() {
        assertEquals(GameLayoutMode.STACKED, AdaptiveLayoutPolicy.gameMode(393f, 852f))
        assertEquals(GameLayoutMode.SPLIT, AdaptiveLayoutPolicy.gameMode(852f, 393f))
        assertEquals(GameLayoutMode.STACKED, AdaptiveLayoutPolicy.gameMode(720f, 1600f))
        assertEquals(GameLayoutMode.SPLIT, AdaptiveLayoutPolicy.gameMode(1600f, 720f))
    }

    @Test
    fun landscapeConsoleIsBoundedAndLeavesMostSpaceForTheMap() {
        assertEquals(320f, AdaptiveLayoutPolicy.consoleWidth(852f), 0.01f)
        assertEquals(440f, AdaptiveLayoutPolicy.consoleWidth(1600f), 0.01f)
        assertTrue(852f - AdaptiveLayoutPolicy.consoleWidth(852f) > AdaptiveLayoutPolicy.consoleWidth(852f))
    }
}
