# Cartarchy for Android

This is the native Kotlin/Jetpack Compose port of the Cartarchy iOS app. It shares the same classic 42-territory topology, rules, AI personalities, monochrome presentation, map outlines, and Cartarchy server QR protocol.

## What works

- Adaptive portrait and landscape phone UI using the black, white, and gray Macintosh-inspired design.
- The exact `cartarchy/board_paths.json` map asset, parsed into native scalable Android paths with touch hit testing.
- One human and one to five configurable cautious, balanced, or aggressive AI commanders.
- Round-robin territory claiming and starting-army placement, with optional balanced random placement for faster setup.
- Reinforcement and continent bonuses, attacks and dice comparisons, captures, elimination, fortification, cards, progressive set trades, and victory.
- Offline game persistence through Android `SharedPreferences`.
- Server configuration and game joining from `cartarchy://configure` and `cartarchy://join` QR payloads.
- Stable app-scoped Android device identity, mythology server profile, profile link, and joined-game list.
- Permissionless QR scanning through Google Code Scanner.

## Toolchain

- Android Gradle Plugin 9.2.0
- Gradle 9.4.1 (included wrapper)
- Kotlin/Compose compiler 2.3.21
- Compose BOM 2026.06.00
- Android SDK 36 / Build Tools 36.0.0
- Minimum Android version: API 23

The project targets API 36 to satisfy the Google Play requirement taking effect August 31, 2026.

## Build and test

Set `sdk.dir` in an untracked `local.properties`, then run:

```sh
cd android
./gradlew testDebugUnitTest assembleDebug
```

The installable development APK is generated at:

```text
app/build/outputs/apk/debug/app-debug.apk
```

The Google Play listing icon is stored at `store-assets/cartarchy-play-icon-512.png`.
Launcher resources include density-specific legacy icons plus adaptive color and
Android 13 monochrome layers.

Run the 42-territory geometry and hit-testing suite on a connected device or emulator:

```sh
./gradlew connectedDebugAndroidTest
```

Run static analysis and build the unsigned release bundle:

```sh
./gradlew lintDebug bundleRelease
```

## Emulator

The local test environment uses an Android 16 Google APIs ARM64 virtual device named `Cartarchy_API36`. Start it with:

```sh
emulator @Cartarchy_API36
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

An Android emulator reaches a server running on the Mac at `http://10.0.2.2:PORT`. Debug builds allow cleartext HTTP for local testing. Release builds require HTTPS.

## Map asset parity

The Android asset is intentionally copied from the iOS source. Verify that the two copies remain identical after editing the map:

```sh
shasum -a 256 \
  cartarchy/board_paths.json \
  android/app/src/main/assets/board_paths.json
```

The matching `MapAttribution.txt` is bundled and exposed from the in-app About / Credits screen.

## Project layout

- `model/` — classic map, models, rules, cards, and bonuses.
- `game/` — state machine, AI, local save codec, and view model.
- `map/` — iOS-derived SVG path parser, transforms, centers, and hit testing.
- `server/` — QR/deep-link parser and Cartarchy HTTP client.
- `ui/` — native Compose menu, setup, board, console, and server screens.

The product label is **Cartarchy**, the formal Google Play listing name is **Diller Cartarchy**, and the permanent application ID is `com.diller.cartarchy`.

See [PLAY_STORE.md](PLAY_STORE.md) before creating a production signing key or submitting a release.
