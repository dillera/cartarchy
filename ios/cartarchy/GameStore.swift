import Foundation
import Observation

@MainActor @Observable
final class GameStore {
    var match: SavedMatch?
    var selected: String?
    var target: String?
    var showingNewGame = false
    var showingRules = false
    var remoteGameID: String?
    var remoteVersion = 0
    var remotePlayerIDs: [String] = []
    var localPlayerIndex: Int?
    var remoteWriteInFlight = false
    var onRemoteChange: ((SavedMatch, String) -> Void)?
    private let saveKey = "cartarchy.savedMatch.v1"
    var isRemote: Bool { remoteGameID != nil }
    var isLocalTurn: Bool { !isRemote || (match?.currentPlayer == localPlayerIndex && !remoteWriteInFlight) }
    var remoteWinnerID: String? {
        guard isRemote, let match, match.phase == .gameOver, remotePlayerIDs.indices.contains(match.currentPlayer) else { return nil }
        return remotePlayerIDs[match.currentPlayer]
    }

    init() {
        if ProcessInfo.processInfo.environment["CARTARCHY_UI_TEST"] == "game" { newGame(); return }
        load()
    }

    func newGame(aiKinds: [PlayerKind]) {
        let aiSelection = Array(aiKinds.prefix(5))
        let players = [Player(id: 0, name: "COMMANDER", kind: .human)] + aiSelection.enumerated().map { index, kind in Player(id: index + 1, name: "CPU \(index + 1)", kind: kind) }
        let states = Dictionary(uniqueKeysWithValues: ClassicMap.territories.map { ($0.id, TerritoryState(owner: -1, armies: 0)) })
        let initial = ClassicRules.initialArmyCount(players: players.count)
        match = SavedMatch(players: players, territories: states, currentPlayer: 0, phase: .claim, reinforcements: 0, turn: 0, message: "Claim one unoccupied territory.", deck: ClassicRules.deck(), discard: [], tradeCount: 0, conqueredThisTurn: false, setupArmies: Array(repeating: initial, count: players.count))
        selected = nil; target = nil; save()
    }

    func newGame(aiCount: Int = 3) { newGame(aiKinds: (0..<aiCount).map { [.cautious, .balanced, .aggressive][$0 % 3] }) }

    func openRemote(game: ServerGame, localPlayerID: String) {
        remoteGameID = game.id; remoteVersion = game.version; remotePlayerIDs = game.playerIds
        remoteWriteInFlight = false
        localPlayerIndex = game.playerIds.firstIndex(of: localPlayerID)
        if let state = game.state {
            match = state
        } else {
            let players = game.players.enumerated().map { Player(id: $0.offset, name: $0.element.name.uppercased(), kind: .human) }
            let states = Dictionary(uniqueKeysWithValues: ClassicMap.territories.map { ($0.id, TerritoryState(owner: -1, armies: 0)) })
            let initial = ClassicRules.initialArmyCount(players: players.count)
            match = SavedMatch(players: players, territories: states, currentPlayer: 0, phase: .claim, reinforcements: 0, turn: 0, message: "Claim one unoccupied territory.", deck: ClassicRules.deck(), discard: [], tradeCount: 0, conqueredThisTurn: false, setupArmies: Array(repeating: initial, count: players.count))
        }
        selected = nil; target = nil
    }

    func applyRemote(_ game: ServerGame) {
        guard game.id == remoteGameID, game.version > remoteVersion else { return }
        remoteVersion = game.version
        if remotePlayerIDs.isEmpty { remotePlayerIDs = game.playerIds }
        var updated = game.state ?? match
        if var state = updated, remotePlayerIDs.count > game.playerIds.count {
            for index in remotePlayerIDs.indices where !game.playerIds.contains(remotePlayerIDs[index]) { state.players[index].isAlive = false }
            if let currentID = game.currentPlayerId, let current = remotePlayerIDs.firstIndex(of: currentID) { state.currentPlayer = current }
            updated = state
        }
        if let updated { match = updated }
    }

    func closeRemote() {
        remoteGameID = nil; remoteVersion = 0; remotePlayerIDs = []; localPlayerIndex = nil; remoteWriteInFlight = false; onRemoteChange = nil
        match = nil; selected = nil; target = nil
    }

    func finishRemoteWrite() { remoteWriteInFlight = false }

    func tap(_ id: String) {
        guard var game = match, game.players[game.currentPlayer].kind == .human, isLocalTurn else { return }
        let state = game.territories[id]!
        switch game.phase {
        case .claim:
            guard state.owner == -1 else { return }
            game.territories[id] = TerritoryState(owner: game.currentPlayer, armies: 1)
            game.setupArmies[game.currentPlayer] -= 1
            advanceSetupTurn(game: &game)
        case .initialDeploy:
            guard state.owner == game.currentPlayer, game.setupArmies[game.currentPlayer] > 0 else { return }
            game.territories[id]!.armies += 1; game.setupArmies[game.currentPlayer] -= 1
            advanceSetupTurn(game: &game)
        case .reinforce:
            if game.players[game.currentPlayer].cards.count >= 5, ClassicRules.validSetIndices(in: game.players[game.currentPlayer].cards) != nil {
                game.message = "You hold five cards. Trade a set before deploying."
                commit(game, kind: "state.updated")
                return
            }
            guard state.owner == game.currentPlayer, game.reinforcements > 0 else { return }
            game.territories[id]!.armies += 1; game.reinforcements -= 1
            game.message = game.reinforcements == 0 ? "Reinforcement complete. Begin attacks." : "\(game.reinforcements) armies remain."
            if game.reinforcements == 0 { game.phase = .attack }
        case .attack:
            if state.owner == game.currentPlayer, state.armies > 1 { selected = id; target = nil; game.message = "Choose an adjacent enemy." }
            else if let source = selected, ClassicMap.byID[source]!.neighbors.contains(id), state.owner != game.currentPlayer { target = id; attack(source: source, destination: id, game: &game) }
        case .fortify:
            if selected == nil, state.owner == game.currentPlayer, state.armies > 1 { selected = id; game.message = "Choose a connected territory." }
            else if let source = selected, state.owner == game.currentPlayer, ClassicMap.byID[source]!.neighbors.contains(id) {
                let moving = game.territories[source]!.armies - 1
                game.territories[source]!.armies = 1; game.territories[id]!.armies += moving; endTurn(game: &game)
            }
        case .gameOver: break
        }
        commit(game, kind: "state.updated")
    }

    func nextPhase() {
        guard var game = match, game.players[game.currentPlayer].kind == .human, isLocalTurn else { return }
        let kind = game.phase == .fortify ? "turn.completed" : "state.updated"
        if game.phase == .attack { game.phase = .fortify; selected = nil; game.message = "Move one army, or end your turn." }
        else if game.phase == .fortify { endTurn(game: &game) }
        commit(game, kind: kind)
    }

    private func attack(source: String, destination: String, game: inout SavedMatch) {
        let attackDice = min(3, game.territories[source]!.armies - 1)
        let defendDice = min(2, game.territories[destination]!.armies)
        let a = (0..<attackDice).map { _ in Int.random(in: 1...6) }.sorted(by: >)
        let d = (0..<defendDice).map { _ in Int.random(in: 1...6) }.sorted(by: >)
        var attackerLoss = 0, defenderLoss = 0
        for i in 0..<min(a.count, d.count) { if a[i] > d[i] { defenderLoss += 1 } else { attackerLoss += 1 } }
        game.territories[source]!.armies -= attackerLoss; game.territories[destination]!.armies -= defenderLoss
        game.message = "Rolled \(a.map(String.init).joined(separator: ",")) vs \(d.map(String.init).joined(separator: ","))."
        if game.territories[destination]!.armies == 0 {
            let move = min(attackDice, game.territories[source]!.armies - 1)
            game.territories[source]!.armies -= move; game.territories[destination] = .init(owner: game.currentPlayer, armies: move)
            game.conqueredThisTurn = true
            game.message += " Territory captured!"
            eliminateAndCheckVictory(game: &game)
        }
    }

    private func endTurn(game: inout SavedMatch) {
        selected = nil; target = nil
        if game.conqueredThisTurn {
            if game.deck.isEmpty { game.deck = game.discard.shuffled(); game.discard.removeAll() }
            if let card = game.deck.popLast() { game.players[game.currentPlayer].cards.append(card) }
        }
        repeat { game.currentPlayer = (game.currentPlayer + 1) % game.players.count } while !game.players[game.currentPlayer].isAlive
        if game.currentPlayer == 0 { game.turn += 1 }
        game.phase = .reinforce; game.reinforcements = ClassicRules.reinforcementCount(owner: game.currentPlayer, states: game.territories); game.conqueredThisTurn = false
        game.message = "\(game.players[game.currentPlayer].name) reinforces."
    }

    private func continueAIIfNeeded() {
        guard var game = match, game.phase != .gameOver else { return }
        var safety = 0
        while game.players[game.currentPlayer].kind != .human, safety < 500 {
            safety += 1; let owner = game.currentPlayer
            if game.phase == .claim {
                if let id = game.territories.filter({ $0.value.owner == -1 }).map(\.key).randomElement() {
                    game.territories[id] = TerritoryState(owner: owner, armies: 1); game.setupArmies[owner] -= 1
                }
                advanceSetupTurn(game: &game); continue
            }
            if game.phase == .initialDeploy {
                let owned = game.territories.filter { $0.value.owner == owner }.map(\.key)
                let borders = owned.filter { id in ClassicMap.byID[id]!.neighbors.contains { game.territories[$0]!.owner != owner } }
                if let id = (borders.isEmpty ? owned : borders).randomElement() { game.territories[id]!.armies += 1; game.setupArmies[owner] -= 1 }
                advanceSetupTurn(game: &game); continue
            }
            let owned = game.territories.filter { $0.value.owner == owner }.map(\.key)
            if game.phase == .reinforce {
                while ClassicRules.validSetIndices(in: game.players[owner].cards) != nil { performTrade(game: &game) }
                while game.reinforcements > 0 {
                    let borders = owned.filter { id in ClassicMap.byID[id]!.neighbors.contains { game.territories[$0]!.owner != owner } }
                    let choice = (borders.isEmpty ? owned : borders).max { game.territories[$0]!.armies < game.territories[$1]!.armies }!
                    game.territories[choice]!.armies += 1; game.reinforcements -= 1
                }; game.phase = .attack
            }
            let candidates = owned.flatMap { source in ClassicMap.byID[source]!.neighbors.filter { game.territories[$0]!.owner != owner }.map { (source, $0) } }
                .filter { game.territories[$0.0]!.armies >= game.territories[$0.1]!.armies + (game.players[owner].kind == .cautious ? 3 : 1) }
            if game.phase == .attack, let move = candidates.max(by: { game.territories[$0.0]!.armies < game.territories[$1.0]!.armies }) { attack(source: move.0, destination: move.1, game: &game) }
            else { game.phase = .fortify; endTurn(game: &game) }
            if game.phase == .gameOver { break }
        }
        match = game; save()
    }

    private func advanceSetupTurn(game: inout SavedMatch) {
        if game.phase == .claim, !game.territories.values.contains(where: { $0.owner == -1 }) { game.phase = .initialDeploy }
        if game.phase == .initialDeploy, game.setupArmies.allSatisfy({ $0 == 0 }) {
            game.currentPlayer = 0; game.phase = .reinforce; game.turn = 1
            game.reinforcements = ClassicRules.reinforcementCount(owner: 0, states: game.territories)
            game.message = "Setup complete. Deploy reinforcements."
            return
        }
        repeat { game.currentPlayer = (game.currentPlayer + 1) % game.players.count }
        while game.phase == .initialDeploy && game.setupArmies[game.currentPlayer] == 0
        let remaining = game.setupArmies[game.currentPlayer]
        game.message = game.phase == .claim ? "\(game.players[game.currentPlayer].name) claims a territory." : "\(game.players[game.currentPlayer].name) places an army. \(remaining) remain."
    }

    private func reinforcementCount(owner: Int, states: [String: TerritoryState]) -> Int {
        ClassicRules.reinforcementCount(owner: owner, states: states)
    }

    private func eliminateAndCheckVictory(game: inout SavedMatch) {
        for i in game.players.indices where game.players[i].isAlive && !game.territories.values.contains(where: { $0.owner == i }) {
            if i != game.currentPlayer { game.players[game.currentPlayer].cards.append(contentsOf: game.players[i].cards); game.players[i].cards.removeAll() }
            game.players[i].isAlive = false
        }
        let alive = game.players.filter(\.isAlive)
        if alive.count == 1 { game.phase = .gameOver; game.message = "\(alive[0].name) conquers the world." }
    }

    var canTradeCards: Bool {
        guard let game = match else { return false }
        return game.phase == .reinforce && ClassicRules.validSetIndices(in: game.players[game.currentPlayer].cards) != nil
    }

    var nextTradeValue: Int { match.map { ClassicRules.tradeValue(number: $0.tradeCount) } ?? 4 }

    func tradeCards() {
        guard var game = match, game.players[game.currentPlayer].kind == .human, isLocalTurn, canTradeCards else { return }
        performTrade(game: &game)
        game.message = "Card set traded. Deploy \(game.reinforcements) armies."
        commit(game, kind: "state.updated")
    }

    func randomizeSetup() {
        guard var game = match, isLocalTurn, game.phase == .claim || game.phase == .initialDeploy else { return }
        let territoryIDs = ClassicMap.territories.map(\.id).shuffled()
        for id in game.territories.keys { game.territories[id] = TerritoryState(owner: -1, armies: 0) }
        for (index, id) in territoryIDs.enumerated() {
            game.territories[id] = TerritoryState(owner: index % game.players.count, armies: 1)
        }
        let startingArmies = ClassicRules.initialArmyCount(players: game.players.count)
        for owner in game.players.indices {
            let owned = territoryIDs.filter { game.territories[$0]?.owner == owner }
            for _ in 0..<(startingArmies - owned.count) {
                if let id = owned.randomElement() { game.territories[id]!.armies += 1 }
            }
        }
        game.setupArmies = Array(repeating: 0, count: game.players.count)
        game.currentPlayer = 0
        game.phase = .reinforce
        game.turn = 1
        game.reinforcements = ClassicRules.reinforcementCount(owner: 0, states: game.territories)
        game.conqueredThisTurn = false
        game.message = "Armies placed randomly. \(game.players[0].name) reinforces."
        selected = nil; target = nil
        commit(game, kind: "state.updated")
    }

    private func performTrade(game: inout SavedMatch) {
        let owner = game.currentPlayer
        guard let indices = ClassicRules.validSetIndices(in: game.players[owner].cards) else { return }
        let cards = indices.map { game.players[owner].cards[$0] }
        game.reinforcements += ClassicRules.tradeValue(number: game.tradeCount); game.tradeCount += 1
        if let territory = cards.compactMap(\.territoryID).first(where: { game.territories[$0]?.owner == owner }) { game.territories[territory]!.armies += 2 }
        for index in indices.sorted(by: >) { game.players[owner].cards.remove(at: index) }
        game.discard.append(contentsOf: cards)
    }

    private func isConnected(_ from: String, _ to: String, owner: Int, states: [String: TerritoryState]) -> Bool {
        var visited: Set<String> = [from], queue = [from]
        while !queue.isEmpty { let here = queue.removeFirst(); if here == to { return true }
            for n in ClassicMap.byID[here]!.neighbors where states[n]?.owner == owner && !visited.contains(n) { visited.insert(n); queue.append(n) }
        }; return false
    }

    func save() { if let match, let data = try? JSONEncoder().encode(match) { UserDefaults.standard.set(data, forKey: saveKey) } }
    func load() { if let data = UserDefaults.standard.data(forKey: saveKey) { match = try? JSONDecoder().decode(SavedMatch.self, from: data) } }
    func deleteSave() { if isRemote { closeRemote() } else { UserDefaults.standard.removeObject(forKey: saveKey); match = nil } }

    private func commit(_ game: SavedMatch, kind: String) {
        match = game
        if isRemote {
            guard let onRemoteChange else { return }
            remoteWriteInFlight = true
            onRemoteChange(game, kind)
        }
        else { save(); continueAIIfNeeded() }
    }
}
