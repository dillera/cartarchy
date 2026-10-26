import Foundation
import Observation
import SwiftUI
import UIKit
import VisionKit

@MainActor @Observable
final class ServerConnection {
    var serverURL = UserDefaults.standard.string(forKey: "server.url") ?? ""
    var pairingCode = ""
    var playerName = UserDefaults.standard.string(forKey: "player.name") ?? "Commander"
    var token = UserDefaults.standard.string(forKey: "server.token") ?? ""
    var profileURL = UserDefaults.standard.string(forKey: "player.profileURL") ?? ""
    var playerID = UserDefaults.standard.string(forKey: "player.id") ?? ""
    var pendingGameID = ""
    var games: [ServerGame] = []
    var selectedGameID: String?
    var status = "NOT CONFIGURED"
    var scanning = false
    private var eventsTask: Task<Void, Never>?
    private var pollingTask: Task<Void, Never>?
    private let liveSyncPolicy = LiveSyncPolicy()
    private var applicationActive = false
    @ObservationIgnored private weak var activeStore: GameStore?
    var isConnected: Bool { !token.isEmpty }
    var selectedGame: ServerGame? { games.first { $0.id == selectedGameID } }

    init() {
        if isConnected {
            Task { await restoreIdentity(); await refreshGames() }
        }
    }

    func accept(scanned value: String) {
        guard let parts = URLComponents(string: value), parts.scheme == "cartarchy" else { status = "NOT A CARTARCHY CODE"; return }
        let values = Dictionary(uniqueKeysWithValues: (parts.queryItems ?? []).map { ($0.name, $0.value ?? "") })
        switch parts.host {
        case "configure":
            guard let server = values["server"], let code = values["code"] else { status = "INCOMPLETE CODE"; return }
            serverURL = server; pairingCode = code; pendingGameID = ""; scanning = false
            Task { await register() }
        case "join":
            guard let server = values["server"], let game = values["game"], let pairing = values["pairing"] else { status = "INCOMPLETE GAME CODE"; return }
            let isCurrentServer = serverURL == server
            serverURL = server; pairingCode = pairing; pendingGameID = game; scanning = false
            if token.isEmpty || !isCurrentServer {
                stopLiveSync()
                token = ""
                Task { await register() }
            } else {
                Task { await joinPendingGame() }
            }
        default:
            status = "UNKNOWN CARTARCHY CODE"
        }
    }

    func register() async {
        stopLiveSync()
        guard let base = URL(string: serverURL), let url = URL(string: "/v1/devices/register", relativeTo: base) else { status = "INVALID SERVER URL"; return }
        status = "CONNECTING…"
        var request = URLRequest(url: url); request.httpMethod = "POST"; request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let deviceID = UIDevice.current.identifierForVendor?.uuidString ?? UUID().uuidString
        request.httpBody = try? JSONSerialization.data(withJSONObject: ["DeviceID":deviceID,"Name":playerName,"PairingCode":pairingCode])
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, http.statusCode == 201,
                  let object = try JSONSerialization.jsonObject(with: data) as? [String: Any], let newToken = object["token"] as? String else { status = "PAIRING REJECTED"; return }
            token = newToken
            if let player = object["player"] as? [String: Any] {
                if let assignedName = player["name"] as? String { playerName = assignedName }
                if let assignedID = player["id"] as? String { playerID = assignedID }
            }
            if let assignedProfileURL = object["profileURL"] as? String { profileURL = assignedProfileURL }
            status = "CONNECTED AS \(playerName.uppercased())"
            UserDefaults.standard.set(serverURL, forKey: "server.url"); UserDefaults.standard.set(playerName, forKey: "player.name"); UserDefaults.standard.set(playerID, forKey: "player.id"); UserDefaults.standard.set(token, forKey: "server.token"); UserDefaults.standard.set(profileURL, forKey: "player.profileURL")
            if !pendingGameID.isEmpty { await joinPendingGame() } else { await refreshGames() }
            startLiveSync()
        } catch { status = "CONNECTION FAILED: \(error.localizedDescription.uppercased())" }
    }

    func joinPendingGame() async {
        guard !pendingGameID.isEmpty, let base = URL(string: serverURL), let url = URL(string: "/v1/games/\(pendingGameID)/join", relativeTo: base) else { status = "INVALID GAME CODE"; return }
        status = "JOINING GAME…"
        var request = URLRequest(url: url); request.httpMethod = "POST"; request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization"); request.setValue("application/json", forHTTPHeaderField: "Content-Type"); request.httpBody = Data("{}".utf8)
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, http.statusCode == 200,
                  let game = try? JSONDecoder().decode(ServerGame.self, from: data) else { status = "GAME JOIN REJECTED"; return }
            pendingGameID = ""; status = "JOINED \(game.name.uppercased())"; await refreshGames()
        } catch { status = "GAME JOIN FAILED: \(error.localizedDescription.uppercased())" }
    }

    func refreshGames() async {
        guard isConnected, let base = URL(string: serverURL), let url = URL(string: "/v1/games", relativeTo: base) else { return }
        if playerID.isEmpty { await restoreIdentity() }
        var request = URLRequest(url: url); request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, http.statusCode == 200 else { status = "GAME REFRESH FAILED"; return }
            let fetched = try JSONDecoder().decode([ServerGame].self, from: data)
            games = fetched.map { candidate in
                guard let existing = games.first(where: { $0.id == candidate.id }), existing.version > candidate.version else { return candidate }
                return existing
            }
            if let selectedGameID, !games.contains(where: { $0.id == selectedGameID }) { self.selectedGameID = nil }
            if let selectedGame { activeStore?.applyRemote(selectedGame) }
        } catch { status = "GAME REFRESH FAILED: \(error.localizedDescription.uppercased())" }
    }

    private func restoreIdentity() async {
        guard playerID.isEmpty, let base = URL(string: serverURL), let url = URL(string: "/v1/stats", relativeTo: base) else { return }
        var request = URLRequest(url: url); request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        guard let (data, response) = try? await URLSession.shared.data(for: request), let http = response as? HTTPURLResponse, http.statusCode == 200,
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any], let id = object["id"] as? String else { return }
        playerID = id; UserDefaults.standard.set(id, forKey: "player.id")
    }

    func open(_ game: ServerGame, in store: GameStore) async {
        let opened = game.status == "paused" ? await resume(game) ?? game : game
        selectedGameID = opened.id
        activeStore = store
        store.openRemote(game: opened, localPlayerID: playerID)
        store.onRemoteChange = { [weak self, weak store] match, kind in
            guard let self, let store else { return }
            Task {
                await self.push(match: match, kind: kind, from: store)
                store.finishRemoteWrite()
            }
        }
        if opened.state == nil, opened.currentPlayerId == playerID, let match = store.match {
            store.remoteWriteInFlight = true
            await push(match: match, kind: "state.initialized", from: store)
            store.finishRemoteWrite()
        }
    }

    private func resume(_ game: ServerGame) async -> ServerGame? {
        guard let base = URL(string: serverURL), let url = URL(string: "/v1/games/\(game.id)/resume", relativeTo: base) else { return nil }
        var request = URLRequest(url: url); request.httpMethod = "POST"; request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization"); request.setValue("application/json", forHTTPHeaderField: "Content-Type"); request.httpBody = Data("{}".utf8)
        guard let (data, response) = try? await URLSession.shared.data(for: request), let http = response as? HTTPURLResponse, http.statusCode == 200,
              let resumed = try? JSONDecoder().decode(ServerGame.self, from: data) else { status = "RESUME FAILED"; return nil }
        replace(resumed)
        return resumed
    }

    func resignSelected(in store: GameStore) async {
        guard let gameID = selectedGameID, let base = URL(string: serverURL), let url = URL(string: "/v1/games/\(gameID)/resign", relativeTo: base) else { return }
        var request = URLRequest(url: url); request.httpMethod = "POST"; request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization"); request.setValue("application/json", forHTTPHeaderField: "Content-Type"); request.httpBody = Data("{}".utf8)
        do {
            let (_, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, http.statusCode == 200 else { status = "RESIGN FAILED"; return }
            selectedGameID = nil; activeStore = nil; store.closeRemote(); status = "RESIGNED GAME"; await refreshGames()
        } catch { status = "RESIGN FAILED: \(error.localizedDescription.uppercased())" }
    }

    private func push(match: SavedMatch, kind: String, from store: GameStore) async {
        guard let game = selectedGame, let base = URL(string: serverURL), let url = URL(string: "/v1/games/\(game.id)/events", relativeTo: base),
              let stateData = try? JSONEncoder().encode(match), let state = try? JSONSerialization.jsonObject(with: stateData) else { return }
        let currentID = store.remotePlayerIDs.indices.contains(match.currentPlayer) ? store.remotePlayerIDs[match.currentPlayer] : ""
        let body: [String: Any] = ["ExpectedVersion": game.version, "Kind": kind, "Payload": ["currentPlayerId": currentID], "State": state]
        var request = URLRequest(url: url); request.httpMethod = "POST"; request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization"); request.setValue("application/json", forHTTPHeaderField: "Content-Type"); request.httpBody = try? JSONSerialization.data(withJSONObject: body)
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, http.statusCode == 200, let updated = try? JSONDecoder().decode(ServerGame.self, from: data) else { await refreshGames(); if let latest = selectedGame { store.applyRemote(latest) }; return }
            replace(updated); store.applyRemote(updated)
            if let winnerID = store.remoteWinnerID { await finish(gameID: game.id, winnerID: winnerID, in: store) }
        } catch { status = "SYNC FAILED: \(error.localizedDescription.uppercased())"; await refreshGames() }
    }

    private func finish(gameID: String, winnerID: String, in store: GameStore) async {
        guard let base = URL(string: serverURL), let url = URL(string: "/v1/games/\(gameID)/finish", relativeTo: base) else { return }
        var request = URLRequest(url: url); request.httpMethod = "POST"; request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization"); request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try? JSONSerialization.data(withJSONObject: ["WinnerID": winnerID])
        guard let (data, response) = try? await URLSession.shared.data(for: request), let http = response as? HTTPURLResponse, http.statusCode == 200,
              let finished = try? JSONDecoder().decode(ServerGame.self, from: data) else { await refreshGames(); return }
        replace(finished); store.applyRemote(finished); status = "GAME COMPLETE"
    }

    private func replace(_ game: ServerGame) {
        if let index = games.firstIndex(where: { $0.id == game.id }) { games[index] = game } else { games.append(game) }
    }

    func setApplicationActive(_ active: Bool) {
        applicationActive = active
        if active {
            startLiveSync()
            if liveSyncPolicy.refreshOnForeground { Task { await refreshGames() } }
        } else {
            stopLiveSync()
        }
    }

    private func startLiveSync() {
        guard isConnected, applicationActive else { return }
        startEventStream()
        startPolling()
    }

    private func stopLiveSync() {
        eventsTask?.cancel(); eventsTask = nil
        pollingTask?.cancel(); pollingTask = nil
    }

    private func startEventStream() {
        guard eventsTask?.isCancelled != false else { return }
        eventsTask = Task { [weak self] in
            guard let self else { return }
            var attempt = 0
            while !Task.isCancelled, applicationActive, isConnected {
                do {
                    let receivedEvent = try await consumeEventStream()
                    attempt = receivedEvent ? 0 : attempt + 1
                } catch is CancellationError {
                    if Task.isCancelled { return }
                    attempt += 1
                    status = "LIVE UPDATES RECONNECTING…"
                } catch {
                    if Task.isCancelled { return }
                    attempt += 1
                    status = "LIVE UPDATES RECONNECTING…"
                }
                guard !Task.isCancelled, applicationActive, isConnected else { return }
                let delay = liveSyncPolicy.reconnectDelaySeconds(attempt: max(0, attempt - 1))
                try? await Task.sleep(nanoseconds: delay * 1_000_000_000)
            }
        }
    }

    private func consumeEventStream() async throws -> Bool {
        guard let base = URL(string: serverURL), let url = URL(string: "/v1/events", relativeTo: base) else { throw URLError(.badURL) }
        var request = URLRequest(url: url); request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization"); request.setValue("text/event-stream", forHTTPHeaderField: "Accept")
        let (bytes, response) = try await URLSession.shared.bytes(for: request)
        guard let http = response as? HTTPURLResponse, http.statusCode == 200 else { throw URLError(.badServerResponse) }
        var receivedEvent = false
        for try await line in bytes.lines {
            try Task.checkCancellation()
            guard line.hasPrefix("data: ") else { continue }
            receivedEvent = true
            await refreshGames()
            if status == "LIVE UPDATES RECONNECTING…" { status = "CONNECTED AS \(playerName.uppercased())" }
        }
        return receivedEvent
    }

    private func startPolling() {
        guard pollingTask?.isCancelled != false else { return }
        pollingTask = Task { [weak self] in
            guard let self else { return }
            while !Task.isCancelled, applicationActive, isConnected {
                try? await Task.sleep(nanoseconds: liveSyncPolicy.pollIntervalSeconds * 1_000_000_000)
                guard !Task.isCancelled, applicationActive, isConnected else { return }
                await refreshGames()
            }
        }
    }

    func disconnect() { stopLiveSync(); token = ""; playerID = ""; pairingCode = ""; pendingGameID = ""; games = []; selectedGameID = nil; status = "NOT CONFIGURED"; UserDefaults.standard.removeObject(forKey: "server.token"); UserDefaults.standard.removeObject(forKey: "player.id") }
}

struct QRScanner: UIViewControllerRepresentable {
    let onScan: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(onScan: onScan) }
    func makeUIViewController(context: Context) -> DataScannerViewController {
        let scanner = DataScannerViewController(recognizedDataTypes: [.barcode(symbologies: [.qr])], qualityLevel: .balanced, recognizesMultipleItems: false, isHighFrameRateTrackingEnabled: false, isPinchToZoomEnabled: true, isGuidanceEnabled: true)
        scanner.delegate = context.coordinator; try? scanner.startScanning(); return scanner
    }
    func updateUIViewController(_ controller: DataScannerViewController, context: Context) { if !controller.isScanning { try? controller.startScanning() } }
    static func dismantleUIViewController(_ controller: DataScannerViewController, coordinator: Coordinator) { controller.stopScanning() }
    final class Coordinator: NSObject, DataScannerViewControllerDelegate {
        let onScan: (String) -> Void
        init(onScan: @escaping (String) -> Void) { self.onScan = onScan }
        func dataScanner(_ dataScanner: DataScannerViewController, didAdd addedItems: [RecognizedItem], allItems: [RecognizedItem]) {
            for item in addedItems { if case let .barcode(code) = item, let value = code.payloadStringValue { onScan(value); return } }
        }
    }
}

struct ServerSetupView: View {
    @Environment(\.dismiss) private var dismiss
    @Bindable var connection: ServerConnection
    var body: some View {
        NavigationStack {
            VStack(spacing: 16) {
                Text("GAME SERVER").font(.title2.bold())
                if connection.scanning && DataScannerViewController.isSupported && DataScannerViewController.isAvailable {
                    QRScanner(onScan: connection.accept).overlay(Rectangle().stroke(.black, lineWidth: 3))
                } else {
                    ScrollView {
                        VStack(spacing: 16) {
                            TextField("https://server.example", text: $connection.serverURL).textInputAutocapitalization(.never).keyboardType(.URL).textFieldStyle(.roundedBorder)
                            TextField("PAIRING CODE", text: $connection.pairingCode).textInputAutocapitalization(.never).textFieldStyle(.roundedBorder)
                            TextField("PLAYER NAME", text: $connection.playerName).textFieldStyle(.roundedBorder)
                            MacButton("SCAN SERVER QR") { connection.scanning = true }
                            MacButton("CONNECT") { Task { await connection.register() } }
                            if connection.isConnected { MacButton("DISCONNECT") { connection.disconnect() } }
                            if let profile = URL(string: connection.profileURL), connection.isConnected { Link("OPEN COMMANDER PROFILE", destination: profile).font(.caption.bold()).foregroundStyle(.black) }
                        }
                    }
                }
                Text(connection.status).font(.caption).frame(maxWidth: .infinity).padding(8).foregroundStyle(.white).background(.black)
            }.padding(24)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("DONE") { dismiss() } } }
        }
    }
}
