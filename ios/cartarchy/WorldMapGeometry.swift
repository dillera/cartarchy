import CoreGraphics

enum WorldMapGeometry {
    typealias P = CGPoint

    // Cross-continent sea routes used by the classic 42-territory topology.
    // Coordinates are aligned to the board geometry, not a geographic map.
    static let visibleSeaRoutes: [(P, P)] = [
        (P(x: 0.333, y: 0.150), P(x: 0.423, y: 0.225)), // Greenland–Iceland
        (P(x: 0.386, y: 0.618), P(x: 0.468, y: 0.585)), // Brazil–North Africa
        (P(x: 0.500, y: 0.430), P(x: 0.480, y: 0.565)), // W. Europe–North Africa
        (P(x: 0.585, y: 0.445), P(x: 0.585, y: 0.565)), // S. Europe–Egypt
        (P(x: 0.655, y: 0.555), P(x: 0.625, y: 0.665)), // Middle East–E. Africa
        (P(x: 0.785, y: 0.620), P(x: 0.805, y: 0.735))  // Siam–Indonesia
    ]

    static func path(for id: String, size: CGSize) -> CGPath {
        BoardGeometry.path(for: id, size: size)
    }

    static func center(for id: String, size: CGSize) -> P {
        let path = path(for: id, size: size)
        let bounds = path.boundingBoxOfPath
        let middle = P(x: bounds.midX, y: bounds.midY)
        if path.contains(middle, using: .evenOdd) { return middle }

        // Concave and multi-island territories can have a bounding-box center
        // over water. Find the closest interior grid point for the army marker.
        var best: (point: P, distance: CGFloat)?
        for row in 1...11 {
            for column in 1...11 {
                let point = P(
                    x: bounds.minX + bounds.width * CGFloat(column) / 12,
                    y: bounds.minY + bounds.height * CGFloat(row) / 12
                )
                guard path.contains(point, using: .evenOdd) else { continue }
                let distance = hypot(point.x - middle.x, point.y - middle.y)
                if best == nil || distance < best!.distance { best = (point, distance) }
            }
        }
        return best?.point ?? middle
    }

    static func territory(at point: P, size: CGSize) -> String? {
        ClassicMap.territories.lazy.map(\.id).first {
            path(for: $0, size: size).contains(point, using: .evenOdd)
        }
    }
}
