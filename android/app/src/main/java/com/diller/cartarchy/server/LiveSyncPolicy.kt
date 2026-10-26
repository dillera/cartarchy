package com.diller.cartarchy.server

class LiveSyncPolicy {
    val pollIntervalSeconds = 15L
    val refreshOnForeground = true

    fun reconnectDelaySeconds(attempt: Int): Long {
        val exponent = attempt.coerceIn(0, 5)
        return (1L shl exponent).coerceAtMost(30L)
    }
}
