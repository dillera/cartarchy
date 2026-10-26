import CoreGraphics
import Foundation

/// Loads the 42 simplified territory outlines bundled with the app.
/// The source SVG uses only absolute M/L/C/Z commands, so this intentionally
/// small parser keeps the map native, scalable, fillable, and hit-testable.
enum BoardGeometry {
    private struct BoardPaths: Decodable {
        let territories: [String: String]
    }

    private static let sourceOrigin = CGPoint(x: 167.99651, y: 118.55507)
    private static let sourceSize = CGSize(width: 749.81909, height: 519.06781)
    // CGPath is immutable after parsing, although CoreGraphics does not mark it Sendable.
    nonisolated(unsafe) private static let sourcePaths: [String: CGPath] = loadPaths()

    static var territoryIDs: Set<String> { Set(sourcePaths.keys) }

    static func path(for id: String, size: CGSize) -> CGPath {
        guard let source = sourcePaths[id] else { return CGMutablePath() }
        var transform = CGAffineTransform(
            a: size.width / sourceSize.width,
            b: 0,
            c: 0,
            d: size.height / sourceSize.height,
            tx: -sourceOrigin.x * size.width / sourceSize.width,
            ty: -sourceOrigin.y * size.height / sourceSize.height
        )
        return source.copy(using: &transform) ?? CGMutablePath()
    }

    private static func loadPaths() -> [String: CGPath] {
#if SWIFT_PACKAGE
        let bundle = Bundle.module
#else
        let bundle = Bundle.main
#endif
        guard let url = bundle.url(forResource: "board_paths", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let board = try? JSONDecoder().decode(BoardPaths.self, from: data) else { return [:] }
        return board.territories.mapValues(SVGPathParser.parse)
    }
}

private enum SVGPathParser {
    static func parse(_ source: String) -> CGPath {
        var scanner = Scanner(string: source)
        let path = CGMutablePath()
        var command: Character?

        while scanner.hasMore {
            if let next = scanner.readCommand() { command = next }
            guard let command else { break }

            switch command {
            case "M":
                guard let point = scanner.readPoint() else { return path }
                path.move(to: point)
                while let point = scanner.readPoint() { path.addLine(to: point) }
            case "L":
                while let point = scanner.readPoint() { path.addLine(to: point) }
            case "C":
                while let control1 = scanner.readPoint(),
                      let control2 = scanner.readPoint(),
                      let end = scanner.readPoint() {
                    path.addCurve(to: end, control1: control1, control2: control2)
                }
            case "Z", "z":
                path.closeSubpath()
            default:
                return path
            }
        }
        return path
    }

    private struct Scanner {
        let characters: [Character]
        var index = 0

        init(string: String) { characters = Array(string) }
        var hasMore: Bool { index < characters.count }

        mutating func readCommand() -> Character? {
            skipSeparators()
            guard hasMore, characters[index].isLetter else { return nil }
            defer { index += 1 }
            return characters[index]
        }

        mutating func readPoint() -> CGPoint? {
            let checkpoint = index
            guard let x = readNumber(), let y = readNumber() else {
                index = checkpoint
                return nil
            }
            return CGPoint(x: x, y: y)
        }

        mutating func readNumber() -> CGFloat? {
            skipSeparators()
            guard hasMore, !characters[index].isLetter else { return nil }
            let start = index
            if characters[index] == "+" || characters[index] == "-" { index += 1 }
            while hasMore, characters[index].isNumber { index += 1 }
            if hasMore, characters[index] == "." {
                index += 1
                while hasMore, characters[index].isNumber { index += 1 }
            }
            if hasMore, characters[index] == "e" || characters[index] == "E" {
                index += 1
                if hasMore, characters[index] == "+" || characters[index] == "-" { index += 1 }
                while hasMore, characters[index].isNumber { index += 1 }
            }
            guard index > start else { return nil }
            return Double(String(characters[start..<index])).map { CGFloat($0) }
        }

        mutating func skipSeparators() {
            while hasMore, characters[index].isWhitespace || characters[index] == "," { index += 1 }
        }
    }
}
