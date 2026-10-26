import Testing
@testable import CartarchyRules

@Test func phonesUseStackedPortraitAndSplitLandscapeLayouts() {
    #expect(AdaptiveLayoutPolicy.gameMode(width: 393, height: 852) == .stacked)
    #expect(AdaptiveLayoutPolicy.gameMode(width: 852, height: 393) == .split)
    #expect(AdaptiveLayoutPolicy.gameMode(width: 720, height: 1600) == .stacked)
    #expect(AdaptiveLayoutPolicy.gameMode(width: 1600, height: 720) == .split)
}

@Test func landscapeConsoleIsBoundedAndLeavesMostSpaceForTheMap() {
    #expect(AdaptiveLayoutPolicy.consoleWidth(totalWidth: 852) == 320)
    #expect(AdaptiveLayoutPolicy.consoleWidth(totalWidth: 1600) == 440)
    #expect(852 - AdaptiveLayoutPolicy.consoleWidth(totalWidth: 852) > AdaptiveLayoutPolicy.consoleWidth(totalWidth: 852))
}
