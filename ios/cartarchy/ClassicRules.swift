import Foundation

enum ClassicRules {
    static func initialArmyCount(players: Int) -> Int {
        switch players { case 2: 40; case 3: 35; case 4: 30; case 5: 25; default: 20 }
    }
    static func reinforcementCount(owner: Int, states: [String: TerritoryState]) -> Int {
        var total = max(3, states.values.filter { $0.owner == owner }.count / 3)
        for continent in Continent.allCases {
            let ids = ClassicMap.territories.filter { $0.continent == continent }.map(\.id)
            if ids.allSatisfy({ states[$0]?.owner == owner }) { total += continent.bonus }
        }
        return total
    }

    static func tradeValue(number: Int) -> Int {
        let opening = [4, 6, 8, 10, 12, 15]
        return number < opening.count ? opening[number] : 15 + (number - opening.count + 1) * 5
    }

    static func validSetIndices(in cards: [CartarchyCard]) -> [Int]? {
        guard cards.count >= 3 else { return nil }
        for a in 0..<(cards.count - 2) {
            for b in (a + 1)..<(cards.count - 1) {
                for c in (b + 1)..<cards.count {
                    let set = [cards[a], cards[b], cards[c]]
                    if isValidSet(set) { return [a, b, c] }
                }
            }
        }
        return nil
    }

    static func isValidSet(_ cards: [CartarchyCard]) -> Bool {
        guard cards.count == 3 else { return false }
        let nonWild = cards.compactMap(\.unit)
        if nonWild.count < 3 { return true }
        return Set(nonWild).count == 1 || Set(nonWild).count == 3
    }

    static func deck() -> [CartarchyCard] {
        let units = CardUnit.allCases
        var cards = ClassicMap.territories.enumerated().map { index, territory in
            CartarchyCard(id: territory.id, territoryID: territory.id, unit: units[index % units.count])
        }
        cards.append(CartarchyCard(id: "wild-1", territoryID: nil, unit: nil))
        cards.append(CartarchyCard(id: "wild-2", territoryID: nil, unit: nil))
        return cards.shuffled()
    }
}
