import Foundation

struct LiveSyncPolicy {
    let pollIntervalSeconds: UInt64 = 15
    let refreshOnForeground = true

    func reconnectDelaySeconds(attempt: Int) -> UInt64 {
        let exponent = min(max(attempt, 0), 5)
        return min(UInt64(1 << exponent), 30)
    }
}
