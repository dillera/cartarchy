# Cartarchy

Native iOS and Android editions of a monochrome strategy game inspired by classic Macintosh world-conquest games, with a self-hosted Linux game server.

The product and in-app name is **Cartarchy**. The formal App Store and Google Play listing name is **Diller Cartarchy**. Internal identifiers use `cartarchy`.

## iOS

The Swift app, package, tests, and Xcode project live under [`ios/`](ios/). Open
`ios/cartarchy.xcodeproj` in Xcode, choose the `cartarchy` scheme and an iPhone
simulator, then press Run. Gameplay uses a stacked portrait layout and a larger
split map-and-console layout in landscape.

## Android

The native Kotlin/Jetpack Compose port is under [`android/`](android/README.md). Build its debug APK with:

```sh
cd android
./gradlew testDebugUnitTest assembleDebug
```

## Server

The Go/SQLite server under [`server/`](server/README.md) provides mobile pairing, profiles, lobbies, real-time and turn-based game records, pause/resume, leaderboards, administration, and QR-based configuration.

Both mobile editions support configurable AI opponents, round-robin setup, the classic 42-territory topology, dice combat, continent bonuses, cards, fortification, elimination, victory, and automatic local save/resume.

## App icons

The shared 1024px globe-and-crossed-swords artwork is under [`artwork/`](artwork/).
iOS consumes it through `Assets.xcassets/AppIcon`, while the Google Play upload
icon is at [`android/store-assets/cartarchy-play-icon-512.png`](android/store-assets/cartarchy-play-icon-512.png).
