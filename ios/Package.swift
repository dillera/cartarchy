// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "CartarchyRules",
    platforms: [.macOS(.v14), .iOS(.v17)],
    products: [.library(name: "CartarchyRules", targets: ["CartarchyRules"])],
    targets: [
        .target(
            name: "CartarchyRules",
            path: "cartarchy",
            exclude: ["ContentView.swift", "CartarchyApp.swift", "ServerConnection.swift", "MapAttribution.txt", "Assets.xcassets"],
            sources: ["Models.swift", "ClassicRules.swift", "WorldMapGeometry.swift", "BoardGeometry.swift", "GameStore.swift", "LiveSyncPolicy.swift", "AdaptiveLayoutPolicy.swift"],
            resources: [.process("board_paths.json")]
        ),
        .testTarget(name: "CartarchyRulesTests", dependencies: ["CartarchyRules"])
    ]
)
