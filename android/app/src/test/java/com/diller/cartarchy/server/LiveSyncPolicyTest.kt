package com.diller.cartarchy.server

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LiveSyncPolicyTest {
    @Test
    fun reconnectsPollsAndRefreshesOnForeground() {
        val policy = LiveSyncPolicy()

        assertEquals(15L, policy.pollIntervalSeconds)
        assertTrue(policy.refreshOnForeground)
        assertEquals(listOf(1L, 2L, 4L, 8L, 16L, 30L, 30L), (0..6).map(policy::reconnectDelaySeconds))
    }
}
