import Foundation

enum Continent: String, Codable, CaseIterable {
    case northAmerica = "N. America", southAmerica = "S. America", europe = "Europe"
    case africa = "Africa", asia = "Asia", australia = "Australia"

    var bonus: Int { switch self { case .northAmerica: 5; case .southAmerica: 2; case .europe: 5; case .africa: 3; case .asia: 7; case .australia: 2 } }
}

struct TerritoryDefinition: Identifiable, Codable, Hashable {
    let id: String
    let name: String
    let continent: Continent
    let x: Double
    let y: Double
    let neighbors: [String]
}

enum PlayerKind: String, Codable, CaseIterable { case human = "Human", cautious = "Cautious AI", balanced = "Balanced AI", aggressive = "Aggressive AI" }
enum CardUnit: String, Codable, CaseIterable { case infantry, cavalry, artillery }
struct CartarchyCard: Identifiable, Codable, Hashable { let id: String; let territoryID: String?; let unit: CardUnit? }

struct Player: Identifiable, Codable, Hashable {
    let id: Int
    var name: String
    var kind: PlayerKind
    var isAlive: Bool = true
    var cards: [CartarchyCard] = []
}

struct TerritoryState: Codable, Hashable { var owner: Int; var armies: Int }
enum TurnPhase: String, Codable { case claim = "CLAIM TERRITORIES", initialDeploy = "PLACE ARMIES", reinforce = "REINFORCE", attack = "ATTACK", fortify = "FORTIFY", gameOver = "GAME OVER" }

struct SavedMatch: Codable {
    var players: [Player]
    var territories: [String: TerritoryState]
    var currentPlayer: Int
    var phase: TurnPhase
    var reinforcements: Int
    var turn: Int
    var message: String
    var deck: [CartarchyCard]
    var discard: [CartarchyCard]
    var tradeCount: Int
    var conqueredThisTurn: Bool
    var setupArmies: [Int]
}

struct ServerPlayer: Codable, Identifiable, Hashable {
    let id: String
    let name: String
}

struct ServerGame: Codable, Identifiable {
    let id: String
    let name: String
    let mode: String
    let status: String
    let hostId: String?
    let playerIds: [String]
    let players: [ServerPlayer]
    let currentPlayerId: String?
    let winnerId: String?
    let maxPlayers: Int
    let currentRound: Int
    let currentTurn: Int
    let version: Int
    let state: SavedMatch?
    let createdAt: String
    let updatedAt: String

    var playLabel: String { state == nil ? "PLAY \(name)" : "RESUME \(name)" }
    var isPlayable: Bool { status == "active" || status == "paused" }
}

enum ClassicMap {
    static let territories: [TerritoryDefinition] = {
        func t(_ id: String, _ name: String, _ c: Continent, _ x: Double, _ y: Double, _ n: [String]) -> TerritoryDefinition {
            .init(id: id, name: name, continent: c, x: x, y: y, neighbors: n)
        }
        return [
            t("alaska","Alaska",.northAmerica,0.06,0.22,["nwTerritory","alberta","kamchatka"]),
            t("nwTerritory","N.W. Territory",.northAmerica,0.15,0.18,["alaska","alberta","ontario","greenland"]),
            t("greenland","Greenland",.northAmerica,0.27,0.14,["nwTerritory","ontario","quebec","iceland"]),
            t("alberta","Alberta",.northAmerica,0.13,0.29,["alaska","nwTerritory","ontario","westernUS"]),
            t("ontario","Ontario",.northAmerica,0.20,0.28,["nwTerritory","greenland","quebec","easternUS","westernUS","alberta"]),
            t("quebec","Quebec",.northAmerica,0.27,0.27,["greenland","ontario","easternUS"]),
            t("westernUS","Western U.S.",.northAmerica,0.15,0.39,["alberta","ontario","easternUS","centralAmerica"]),
            t("easternUS","Eastern U.S.",.northAmerica,0.23,0.39,["ontario","quebec","westernUS","centralAmerica"]),
            t("centralAmerica","Central America",.northAmerica,0.19,0.50,["westernUS","easternUS","venezuela"]),
            t("venezuela","Venezuela",.southAmerica,0.27,0.57,["centralAmerica","peru","brazil"]),
            t("peru","Peru",.southAmerica,0.29,0.69,["venezuela","brazil","argentina"]),
            t("brazil","Brazil",.southAmerica,0.36,0.64,["venezuela","peru","argentina","northAfrica"]),
            t("argentina","Argentina",.southAmerica,0.32,0.82,["peru","brazil"]),
            t("iceland","Iceland",.europe,0.42,0.20,["greenland","greatBritain","scandinavia"]),
            t("greatBritain","Great Britain",.europe,0.43,0.31,["iceland","scandinavia","northernEurope","westernEurope"]),
            t("scandinavia","Scandinavia",.europe,0.51,0.21,["iceland","greatBritain","northernEurope","ukraine"]),
            t("ukraine","Ukraine",.europe,0.59,0.29,["scandinavia","northernEurope","southernEurope","ural","afghanistan","middleEast"]),
            t("northernEurope","Northern Europe",.europe,0.51,0.34,["greatBritain","scandinavia","ukraine","southernEurope","westernEurope"]),
            t("westernEurope","Western Europe",.europe,0.44,0.42,["greatBritain","northernEurope","southernEurope","northAfrica"]),
            t("southernEurope","Southern Europe",.europe,0.52,0.42,["westernEurope","northernEurope","ukraine","middleEast","egypt","northAfrica"]),
            t("northAfrica","North Africa",.africa,0.46,0.57,["brazil","westernEurope","southernEurope","egypt","eastAfrica","congo"]),
            t("egypt","Egypt",.africa,0.53,0.54,["northAfrica","southernEurope","middleEast","eastAfrica"]),
            t("eastAfrica","East Africa",.africa,0.57,0.64,["egypt","northAfrica","congo","southAfrica","madagascar","middleEast"]),
            t("congo","Congo",.africa,0.50,0.68,["northAfrica","eastAfrica","southAfrica"]),
            t("southAfrica","South Africa",.africa,0.52,0.82,["congo","eastAfrica","madagascar"]),
            t("madagascar","Madagascar",.africa,0.61,0.80,["southAfrica","eastAfrica"]),
            t("ural","Ural",.asia,0.66,0.25,["ukraine","siberia","china","afghanistan"]),
            t("siberia","Siberia",.asia,0.73,0.19,["ural","yakutsk","irkutsk","mongolia","china"]),
            t("yakutsk","Yakutsk",.asia,0.82,0.16,["siberia","irkutsk","kamchatka"]),
            t("kamchatka","Kamchatka",.asia,0.91,0.20,["yakutsk","irkutsk","mongolia","japan","alaska"]),
            t("irkutsk","Irkutsk",.asia,0.80,0.27,["siberia","yakutsk","kamchatka","mongolia"]),
            t("mongolia","Mongolia",.asia,0.80,0.36,["siberia","irkutsk","kamchatka","japan","china"]),
            t("japan","Japan",.asia,0.91,0.38,["kamchatka","mongolia"]),
            t("afghanistan","Afghanistan",.asia,0.65,0.39,["ukraine","ural","china","india","middleEast"]),
            t("middleEast","Middle East",.asia,0.61,0.50,["ukraine","southernEurope","egypt","eastAfrica","india","afghanistan"]),
            t("india","India",.asia,0.70,0.52,["middleEast","afghanistan","china","siam"]),
            t("china","China",.asia,0.75,0.42,["ural","siberia","mongolia","afghanistan","india","siam"]),
            t("siam","Siam",.asia,0.78,0.55,["india","china","indonesia"]),
            t("indonesia","Indonesia",.australia,0.79,0.67,["siam","newGuinea","westernAustralia"]),
            t("newGuinea","New Guinea",.australia,0.88,0.68,["indonesia","westernAustralia","easternAustralia"]),
            t("westernAustralia","Western Australia",.australia,0.82,0.80,["indonesia","newGuinea","easternAustralia"]),
            t("easternAustralia","Eastern Australia",.australia,0.90,0.82,["newGuinea","westernAustralia"])
        ]
    }()
    static let byID = Dictionary(uniqueKeysWithValues: territories.map { ($0.id, $0) })
}
