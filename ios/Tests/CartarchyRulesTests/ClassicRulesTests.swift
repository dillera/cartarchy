import Foundation
import Testing
@testable import CartarchyRules

@Test func progressiveCardTradeValues() {
    #expect((0...8).map(ClassicRules.tradeValue) == [4,6,8,10,12,15,20,25,30])
}

@Test func initialArmiesFollowClassicPlayerCounts() {
    #expect((2...6).map(ClassicRules.initialArmyCount) == [40,35,30,25,20])
}

@Test func cardSetsAcceptMatchingMixedAndWildCombinations() {
    func card(_ unit: CardUnit?) -> CartarchyCard { CartarchyCard(id: UUID().uuidString, territoryID: nil, unit: unit) }
    #expect(ClassicRules.isValidSet([card(.infantry),card(.infantry),card(.infantry)]))
    #expect(ClassicRules.isValidSet([card(.infantry),card(.cavalry),card(.artillery)]))
    #expect(ClassicRules.isValidSet([card(.infantry),card(.infantry),card(nil)]))
    #expect(!ClassicRules.isValidSet([card(.infantry),card(.infantry),card(.cavalry)]))
}

@Test func reinforcementsUseTerritoriesMinimumAndContinentBonuses() {
    var states = Dictionary(uniqueKeysWithValues: ClassicMap.territories.map { ($0.id, TerritoryState(owner: 1, armies: 1)) })
    #expect(ClassicRules.reinforcementCount(owner: 1, states: states) == 38)
    for id in ClassicMap.territories.filter({ $0.continent != .australia }).map(\.id) { states[id]!.owner = 0 }
    #expect(ClassicRules.reinforcementCount(owner: 1, states: states) == 5)
}

@Test func deckContainsEveryTerritoryAndTwoWildCards() {
    let deck = ClassicRules.deck()
    #expect(deck.count == 44)
    #expect(deck.filter { $0.unit == nil }.count == 2)
    #expect(Set(deck.compactMap(\.territoryID)).count == 42)
}

@Test func boardGeometryContainsAllTerritoriesAndSupportsHitTesting() {
    let expected = Set(ClassicMap.territories.map(\.id))
    #expect(BoardGeometry.territoryIDs == expected)
    let phoneBoard = CGSize(width: 393, height: 393 * 0.65)
    for territory in ClassicMap.territories {
        let path = WorldMapGeometry.path(for: territory.id, size: phoneBoard)
        #expect(!path.isEmpty, "Missing map path for \(territory.id)")
        let marker = WorldMapGeometry.center(for: territory.id, size: phoneBoard)
        #expect(path.contains(marker, using: .evenOdd), "Marker is outside \(territory.id)")
        #expect(WorldMapGeometry.territory(at: marker, size: phoneBoard) == territory.id)
    }
}

@Test func easternUSRetainsReadableSoutheastAtPhoneSize() {
    let phoneBoard = CGSize(width: 393, height: 393 * 0.65)
    let bounds = WorldMapGeometry.path(for: "easternUS", size: phoneBoard).boundingBoxOfPath
    #expect(bounds.width >= 43)
    #expect(bounds.height >= 38)
}

@Test func serverGameListDecodesMultipleNamedGames() throws {
    let data = Data(#"[{"id":"one","name":"First Front","mode":"turn-based","status":"active","hostId":"pollux","playerIds":["pollux","castor"],"players":[{"id":"pollux","name":"Pollux"},{"id":"castor","name":"Castor"}],"currentPlayerId":"pollux","maxPlayers":2,"currentRound":1,"currentTurn":1,"version":3,"createdAt":"now","updatedAt":"now"},{"id":"two","name":"Second Front","mode":"turn-based","status":"paused","hostId":"castor","playerIds":["castor","pollux"],"players":[{"id":"castor","name":"Castor"},{"id":"pollux","name":"Pollux"}],"currentPlayerId":"castor","maxPlayers":2,"currentRound":2,"currentTurn":3,"version":8,"createdAt":"now","updatedAt":"now"}]"#.utf8)
    let games = try JSONDecoder().decode([ServerGame].self, from: data)
    #expect(games.map(\.name) == ["First Front", "Second Front"])
    #expect(games[0].players.map(\.name) == ["Pollux", "Castor"])
    #expect(games[1].status == "paused")
}

@MainActor @Test func remoteBoardOnlyAcceptsTheLocalPlayersTurn() throws {
    let data = Data(#"{"id":"one","name":"Twins","mode":"turn-based","status":"active","hostId":"pollux","playerIds":["pollux","castor"],"players":[{"id":"pollux","name":"Pollux"},{"id":"castor","name":"Castor"}],"currentPlayerId":"pollux","maxPlayers":2,"currentRound":1,"currentTurn":1,"version":3,"createdAt":"now","updatedAt":"now"}"#.utf8)
    let game = try JSONDecoder().decode(ServerGame.self, from: data)
    let guestStore = GameStore()
    guestStore.openRemote(game: game, localPlayerID: "castor")
    guestStore.tap("alaska")
    #expect(guestStore.match?.territories["alaska"]?.owner == -1)

    let hostStore = GameStore()
    hostStore.openRemote(game: game, localPlayerID: "pollux")
    var updateKind: String?
    hostStore.onRemoteChange = { _, kind in updateKind = kind }
    hostStore.tap("alaska")
    #expect(hostStore.match?.territories["alaska"]?.owner == 0)
    #expect(updateKind == "state.updated")
}

@MainActor @Test func remoteBoardWaitsForServerBeforeAcceptingAnotherMove() throws {
    let data = Data(#"{"id":"one","name":"Twins","mode":"turn-based","status":"active","hostId":"pollux","playerIds":["pollux","castor"],"players":[{"id":"pollux","name":"Pollux"},{"id":"castor","name":"Castor"}],"currentPlayerId":"pollux","maxPlayers":2,"currentRound":1,"currentTurn":1,"version":3,"createdAt":"now","updatedAt":"now"}"#.utf8)
    let game = try JSONDecoder().decode(ServerGame.self, from: data)
    let store = GameStore()
    store.openRemote(game: game, localPlayerID: "pollux")
    var match = try #require(store.match)
    match.phase = .reinforce
    match.reinforcements = 2
    match.territories["alaska"] = TerritoryState(owner: 0, armies: 1)
    store.match = match
    store.onRemoteChange = { _, _ in }

    store.tap("alaska")
    store.tap("alaska")

    #expect(store.match?.territories["alaska"]?.armies == 2)
    #expect(store.match?.reinforcements == 1)
    #expect(!store.isLocalTurn)
}

@MainActor @Test func remoteGameOverMapsWinnerBackToServerPlayer() throws {
    let data = Data(#"{"id":"one","name":"Twins","mode":"turn-based","status":"active","hostId":"pollux","playerIds":["pollux","castor"],"players":[{"id":"pollux","name":"Pollux"},{"id":"castor","name":"Castor"}],"currentPlayerId":"castor","maxPlayers":2,"currentRound":1,"currentTurn":2,"version":4,"createdAt":"now","updatedAt":"now"}"#.utf8)
    let game = try JSONDecoder().decode(ServerGame.self, from: data)
    let store = GameStore()
    store.openRemote(game: game, localPlayerID: "castor")
    var match = try #require(store.match)
    match.currentPlayer = 1
    match.phase = .gameOver
    store.match = match

    #expect(store.remoteWinnerID == "castor")
}

@Test func liveSyncPolicyReconnectsPollsAndRefreshesOnForeground() {
    let policy = LiveSyncPolicy()

    #expect(policy.pollIntervalSeconds == 15)
    #expect(policy.refreshOnForeground)
    #expect((0...6).map(policy.reconnectDelaySeconds) == [1, 2, 4, 8, 16, 30, 30])
}

@MainActor @Test func randomSetupPlacesEveryStartingArmyAndBeginsFirstTurn() throws {
    let store = GameStore()
    store.newGame(aiKinds: [.cautious, .balanced, .aggressive])
    store.tap("alaska")

    store.randomizeSetup()
    let game = try #require(store.match)
    #expect(game.phase == .reinforce)
    #expect(game.currentPlayer == 0)
    #expect(game.turn == 1)
    #expect(game.setupArmies.allSatisfy { $0 == 0 })
    #expect(game.territories.values.allSatisfy { game.players.indices.contains($0.owner) && $0.armies >= 1 })
    for owner in game.players.indices {
        #expect(game.territories.values.filter { $0.owner == owner }.map(\.armies).reduce(0, +) == 30)
    }
    let territoryCounts = game.players.indices.map { owner in game.territories.values.filter { $0.owner == owner }.count }
    #expect((territoryCounts.max() ?? 0) - (territoryCounts.min() ?? 0) <= 1)
    store.deleteSave()
}
