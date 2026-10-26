import CoreGraphics

enum GameLayoutMode {
    case stacked
    case split
}

enum AdaptiveLayoutPolicy {
    static func gameMode(width: CGFloat, height: CGFloat) -> GameLayoutMode {
        width > height ? .split : .stacked
    }

    static func consoleWidth(totalWidth: CGFloat) -> CGFloat {
        min(440, max(320, totalWidth * 0.375))
    }
}
