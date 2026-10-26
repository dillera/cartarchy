import SwiftUI

struct ContentView: View {
    @Environment(\.scenePhase) private var scenePhase
    @State private var store = GameStore()
    @State private var connection = ServerConnection()
    @State private var confirmingResignation = false
    var body: some View {
        ZStack {
            Color(white: 0.91).ignoresSafeArea()
            if store.match == nil { MainMenu(store: store, connection: connection) }
            else { GameView(store: store, onResign: store.isRemote ? { confirmingResignation = true } : nil) }
        }
        .preferredColorScheme(.light)
        .fontDesign(.monospaced)
        .confirmationDialog("Resign this game? Other players will be notified.", isPresented: $confirmingResignation, titleVisibility: .visible) {
            Button("RESIGN GAME", role: .destructive) { Task { await connection.resignSelected(in: store) } }
            Button("CANCEL", role: .cancel) {}
        }
        .onChange(of: connection.games.map(\.version)) {
            if let game = connection.selectedGame { store.applyRemote(game) }
        }
        .task { connection.setApplicationActive(scenePhase == .active) }
        .onChange(of: scenePhase) { connection.setApplicationActive(scenePhase == .active) }
    }
}

struct MainMenu: View {
    @Bindable var store: GameStore
    @Bindable var connection: ServerConnection
    @State private var showingServer = false
    @State private var showingNewGame = ProcessInfo.processInfo.environment["CARTARCHY_UI_TEST"] == "setup"
    var body: some View {
        GeometryReader { geometry in
            ScrollView {
                VStack(spacing: 24) {
                    Spacer(minLength: 12)
                    Text("Cartarchy").font(.system(size: 34, weight: .black, design: .monospaced)).tracking(-2)
                    Text("A GAME OF GLOBAL STRATEGY").font(.caption.bold())
                    Rectangle().fill(.black).frame(width: 250, height: 3)
                    VStack(spacing: 12) {
                        MacButton("NEW CAMPAIGN") { showingNewGame = true }
                        MacButton("CONTINUE SAVED GAME") { store.load() }.disabled(store.match == nil)
                        MacButton("GAME SERVER…") { showingServer = true }
                        if connection.isConnected {
                            ScrollView {
                                VStack(spacing: 10) {
                                    ForEach(connection.games.filter(\.isPlayable)) { game in
                                        MacButton(game.playLabel.uppercased()) { Task { await connection.open(game, in: store) } }
                                    }
                                    ForEach(connection.games.filter { !$0.isPlayable && $0.status == "waiting" }) { game in
                                        Text("WAITING: \(game.name.uppercased())  \(game.playerIds.count)/\(game.maxPlayers)").font(.caption.bold())
                                    }
                                }
                            }.frame(maxHeight: 220)
                            MacButton("REFRESH SERVER GAMES") { Task { await connection.refreshGames() } }
                        }
                    }.frame(width: 280)
                    Text("OFFLINE + SERVER EDITION").font(.caption2)
                    Text("iOS  •  PORTRAIT + LANDSCAPE").font(.caption2)
                    Spacer(minLength: 12)
                }
                .frame(maxWidth: .infinity, minHeight: geometry.size.height)
                .padding()
            }
        }
        .task { await connection.refreshGames() }
        .sheet(isPresented: $showingServer, onDismiss: { Task { await connection.refreshGames() } }) { ServerSetupView(connection: connection) }
        .sheet(isPresented: $showingNewGame) { NewGameSetupView(store: store) }
    }
}

struct NewGameSetupView: View {
    @Environment(\.dismiss) private var dismiss
    @Bindable var store: GameStore
    @State private var aiKinds: [PlayerKind] = [.cautious, .balanced, .aggressive]
    private let choices: [PlayerKind] = [.cautious, .balanced, .aggressive]
    var body: some View {
        NavigationStack {
            VStack(spacing: 14) {
                Text("NEW CAMPAIGN").font(.title2.bold())
                Text("SELECT COMPUTER COMMANDERS").font(.caption)
                HStack {
                    MacButton("−") { if aiKinds.count > 1 { aiKinds.removeLast() } }
                    Text("\(aiKinds.count) AI  •  \(aiKinds.count + 1) PLAYERS").font(.headline).frame(minWidth: 170)
                    MacButton("+") { if aiKinds.count < 5 { aiKinds.append(choices[aiKinds.count % choices.count]) } }
                }
                ScrollView {
                    VStack(spacing: 10) {
                        HStack { Text("COMMANDER").bold(); Spacer(); Text("HUMAN") }.padding(12).background(.white).overlay(Rectangle().stroke(.black, lineWidth: 2))
                        ForEach(aiKinds.indices, id: \.self) { index in
                            HStack {
                                Text("CPU \(index + 1)").bold()
                                Spacer()
                                Picker("ABILITY", selection: $aiKinds[index]) {
                                    ForEach(choices, id: \.self) { Text($0.rawValue.uppercased()).tag($0) }
                                }.labelsHidden().pickerStyle(.menu).tint(.black)
                            }.padding(8).background(.white).overlay(Rectangle().stroke(.black, lineWidth: 2))
                        }
                    }
                }
                Text("Territories and starting armies will be placed one at a time in player order.").font(.caption).multilineTextAlignment(.center)
                MacButton("BEGIN PLACEMENT") { store.newGame(aiKinds: aiKinds); dismiss() }
            }.padding(20)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("CANCEL") { dismiss() } } }
        }
    }
}

struct GameView: View {
    @Bindable var store: GameStore
    var onResign: (() -> Void)?
    var body: some View {
        if let game = store.match {
            GeometryReader { geo in
                VStack(spacing: 0) {
                    StatusBar(game: game)
                    if AdaptiveLayoutPolicy.gameMode(width: geo.size.width, height: geo.size.height) == .split {
                        HStack(spacing: 0) {
                            BoardView(store: store, game: game)
                                .frame(maxWidth: .infinity, maxHeight: .infinity)
                            GameConsole(store: store, game: game, onResign: onResign)
                                .frame(width: AdaptiveLayoutPolicy.consoleWidth(totalWidth: geo.size.width))
                                .frame(maxHeight: .infinity)
                        }
                    } else {
                        BoardView(store: store, game: game)
                            .frame(height: geo.size.width * 0.65)
                        Spacer(minLength: 4)
                        GameConsole(store: store, game: game, onResign: onResign)
                    }
                }
            }
        }
    }
}

struct GameConsole: View {
    @Bindable var store: GameStore
    let game: SavedMatch
    var onResign: (() -> Void)?
    var body: some View {
        let localOwner = store.isRemote ? (store.localPlayerIndex ?? 0) : 0
        VStack(spacing: 7) {
            ScrollView {
                VStack(spacing: 7) {
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(game.turn == 0 ? "SETUP" : "TURN \(game.turn)").bold()
                            Text(game.phase.rawValue).font(.caption2)
                        }
                        Spacer()
                        VStack(alignment: .trailing, spacing: 2) {
                            Text(game.players[game.currentPlayer].name).bold()
                            if game.phase == .claim || game.phase == .initialDeploy { Text("\(game.setupArmies[game.currentPlayer]) TO PLACE").font(.caption2) }
                            if game.phase == .reinforce { Text("\(game.reinforcements) TO DEPLOY").font(.caption2) }
                        }
                    }
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 6) {
                            ForEach(game.players) { player in
                                let territories = game.territories.values.filter { $0.owner == player.id }.count
                                let armies = game.territories.values.filter { $0.owner == player.id }.map(\.armies).reduce(0,+)
                                VStack(alignment: .leading, spacing: 2) {
                                    HStack { Text(player.name).bold(); if player.id == game.currentPlayer { Text("▶") } }
                                    Text(player.kind.rawValue.uppercased()).font(.system(size: 8, design: .monospaced))
                                    Text("LAND \(territories)  ARMY \(armies)").font(.system(size: 9, design: .monospaced))
                                    Text("CARDS \(player.cards.count)  \(player.isAlive ? "ACTIVE" : "OUT")").font(.system(size: 9, design: .monospaced))
                                }.padding(7).frame(width: 145, alignment: .leading)
                                    .background(player.id == game.currentPlayer ? Color(white: 0.78) : .white)
                                    .overlay(Rectangle().stroke(.black, lineWidth: player.id == game.currentPlayer ? 3 : 1))
                            }
                        }
                    }.frame(height: 70)
                    HStack(spacing: 10) {
                        TerritoryPatternSwatch(owner: localOwner).frame(width: 38, height: 38)
                        VStack(alignment: .leading, spacing: 2) {
                            Text("YOUR TERRITORY PATTERN").font(.system(size: 10, weight: .bold, design: .monospaced))
                            Text(game.players[localOwner].name).font(.system(size: 9, design: .monospaced))
                        }
                        Spacer()
                    }.padding(7).background(Color(white: 0.91)).overlay(Rectangle().stroke(.black, lineWidth: 2))
                    Text(game.message).font(.caption).lineLimit(2).frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            if game.phase == .claim || game.phase == .initialDeploy {
                MacButton("RANDOMIZE ALL ARMIES") { store.randomizeSetup() }
                    .disabled(!store.isLocalTurn).opacity(store.isLocalTurn ? 1 : 0.45)
            }
            HStack(spacing: 10) {
                MacButton(store.isRemote ? "GAMES" : "QUIT") { store.deleteSave() }
                if let onResign { MacButton("RESIGN", action: onResign) }
                if game.phase == .claim { Text("TAP AN UNCLAIMED LAND").font(.caption2.bold()).frame(maxWidth: .infinity) }
                if game.phase == .initialDeploy { Text("TAP YOUR LAND TO PLACE 1").font(.caption2.bold()).frame(maxWidth: .infinity) }
                if game.phase == .reinforce && store.canTradeCards { MacButton("TRADE +\(store.nextTradeValue)") { store.tradeCards() } }
                if game.phase == .attack { MacButton("END ATTACKS") { store.nextPhase() } }
                if game.phase == .fortify { MacButton("END TURN") { store.nextPhase() } }
            }
            if store.isRemote && !store.isLocalTurn { Text("WAITING FOR OPPONENT").font(.caption2.bold()) }
        }.padding(9).background(Color.white).overlay(alignment: .top) { Rectangle().frame(height: 3) }
    }
}

struct StatusBar: View {
    let game: SavedMatch
    var body: some View {
        HStack {
            Text(game.turn == 0 ? "SETUP" : "TURN \(game.turn)")
            Spacer()
            Text(game.players[game.currentPlayer].name).bold()
            Text("CARDS \(game.players[game.currentPlayer].cards.count)")
            Spacer()
            Text(game.phase.rawValue)
            if game.phase == .reinforce { Text("[\(game.reinforcements)]").bold() }
        }.font(.caption).padding(8).foregroundStyle(.white).background(.black)
    }
}

struct BoardView: View {
    @Bindable var store: GameStore
    let game: SavedMatch
    var body: some View {
        GeometryReader { proxy in
          Canvas { context, size in
            let routeStyle = StrokeStyle(lineWidth: 2, lineCap: .round, dash: [7, 5])
            for territory in ClassicMap.territories {
                let path = Path(WorldMapGeometry.path(for: territory.id, size: size))
                let owner = game.territories[territory.id]!.owner
                drawTerritoryPattern(context: &context, path: path, size: size, owner: owner)
                context.stroke(path, with: .color(.black), style: StrokeStyle(lineWidth: store.selected == territory.id ? 4 : 1.2, lineJoin: .round))
            }
            for (start, end) in WorldMapGeometry.visibleSeaRoutes {
                var route = Path()
                route.move(to: CGPoint(x: start.x * size.width, y: start.y * size.height))
                route.addLine(to: CGPoint(x: end.x * size.width, y: end.y * size.height))
                context.stroke(route, with: .color(.black), style: routeStyle)
            }
            var westWrap = Path()
            westWrap.move(to: CGPoint(x: 0, y: size.height * 0.22))
            westWrap.addLine(to: CGPoint(x: size.width * 0.055, y: size.height * 0.22))
            context.stroke(westWrap, with: .color(.black), style: routeStyle)
            var eastWrap = Path()
            eastWrap.move(to: CGPoint(x: size.width * 0.94, y: size.height * 0.22))
            eastWrap.addLine(to: CGPoint(x: size.width, y: size.height * 0.22))
            context.stroke(eastWrap, with: .color(.black), style: routeStyle)
            for t in ClassicMap.territories {
                let center = WorldMapGeometry.center(for: t.id, size: size)
                let armies = game.territories[t.id]!.armies
                guard armies > 0 else { continue }
                let marker = CGRect(x: center.x - 10, y: center.y - 10, width: 20, height: 20)
                context.fill(Path(ellipseIn: marker), with: .color(.white))
                context.stroke(Path(ellipseIn: marker), with: .color(.black), lineWidth: 1.5)
                context.draw(Text("\(armies)").font(.system(size: 9, weight: .black, design: .monospaced)), at: center)
            }
          }
          .background(Color(white: 0.91))
          .contentShape(Rectangle())
          .gesture(DragGesture(minimumDistance: 0).onEnded { value in
              if let id = WorldMapGeometry.territory(at: value.location, size: proxy.size) { store.tap(id) }
          })
        }
        .aspectRatio(1000.0 / 650.0, contentMode: .fit)
        .overlay(Rectangle().stroke(.black, lineWidth: 2))
    }
}

struct TerritoryPatternSwatch: View {
    let owner: Int
    var body: some View {
        Canvas { context, size in
            drawTerritoryPattern(context: &context, path: Path(CGRect(origin: .zero, size: size)), size: size, owner: owner)
        }.overlay(Rectangle().stroke(.black, lineWidth: 2))
    }
}

private func territoryFill(owner: Int) -> Color {
    switch owner {
    case -1: Color(white: 0.91)
    case 0: .white
    case 1: Color(white: 0.09)
    case 2: Color(white: 0.82)
    case 3: .white
    case 4: Color(white: 0.54)
    default: Color(white: 0.88)
    }
}

private func drawTerritoryPattern(context: inout GraphicsContext, path: Path, size: CGSize, owner: Int) {
    context.drawLayer { layer in
        layer.fill(path, with: .color(territoryFill(owner: owner)), style: FillStyle(eoFill: true))
        guard owner >= 2 else { return }
        layer.clip(to: path, style: FillStyle(eoFill: true))
        let spacing: CGFloat = owner == 2 ? 8 : (owner == 3 ? 11 : 7)
        stride(from: -size.height, through: size.width, by: spacing).forEach { x in
            var line = Path(); line.move(to: CGPoint(x: x, y: 0)); line.addLine(to: CGPoint(x: x + size.height, y: size.height))
            layer.stroke(line, with: .color(.black.opacity(owner == 2 ? 0.45 : 0.8)), lineWidth: owner == 2 ? 1 : 1.5)
        }
        if owner >= 5 {
            stride(from: CGFloat.zero, through: size.width + size.height, by: spacing).forEach { x in
                var line = Path(); line.move(to: CGPoint(x: x, y: 0)); line.addLine(to: CGPoint(x: x - size.height, y: size.height))
                layer.stroke(line, with: .color(.black.opacity(0.55)), lineWidth: 1)
            }
        }
    }
}

struct MacButton: View {
    let title: String; let action: () -> Void
    init(_ title: String, action: @escaping () -> Void) { self.title = title; self.action = action }
    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.caption.bold())
                .frame(maxWidth: .infinity)
                .padding(.vertical, 9)
                .foregroundStyle(.black)
                .background(.white)
                .overlay(Rectangle().stroke(.black, lineWidth: 2))
                .background { Rectangle().fill(.black).offset(x: 3, y: 3) }
        }.buttonStyle(.plain)
    }
}
