# Google Play release checklist

This checklist reflects Google Play requirements verified on August 18, 2026. Recheck the linked policies immediately before submission because Play requirements change.

## Release blockers

1. **Complete the intellectual-property review.** The product is now independently branded as Cartarchy, but its familiar 42-territory topology and attributed third-party map geometry still need documented clearance or replacement with wholly original presentation after advice from an IP attorney. Google Play requires original material or documented permission for protected third-party material. [Google Play intellectual-property policy](https://support.google.com/googleplay/android-developer/answer/9888072)
2. **Add profile/data deletion.** The server creates a persistent UUID profile and keeps a device identifier, display name, timezone, game membership, and win/loss history. Before production, add an in-app deletion path, an external deletion-request page, and server-side deletion/retention handling. Disclose any records that must be retained. [Account deletion requirements](https://support.google.com/googleplay/android-developer/answer/13327111)
3. **Publish a privacy policy and complete Data safety accurately.** Document the optional self-hosted server, all fields stored by it, retention, deletion, security, and the Google Code Scanner dependency. Production builds already reject cleartext HTTP; deploy the public server over HTTPS. [Data safety form](https://support.google.com/googleplay/android-developer/answer/10787469)
4. **Confirm the permanent application ID.** The project now uses `com.diller.cartarchy`. Treat it as permanent before the first Play upload; changing it later creates a different app.

## Developer account

- Create a Play Console personal or organization account, pay the one-time US$25 registration fee, complete identity/contact/device verification, and expose the required public developer information. Organization accounts require a D-U-N-S number. [Play Console setup](https://support.google.com/googleplay/android-developer/answer/6112435)
- New personal accounts created after November 13, 2023 must run a closed test with at least 12 continuously opted-in testers for 14 days, then apply for production access. [Testing requirement](https://support.google.com/googleplay/android-developer/answer/14151465)

## App and server readiness

- Keep `compileSdk` and `targetSdk` at 36 or newer. New apps and updates must target API 36 starting August 31, 2026. [Target API policy](https://developer.android.com/google/play/requirements/target-sdk)
- Deploy a stable HTTPS Cartarchy server and publish support, privacy, and deletion URLs on the same controlled domain.
- Add an in-app privacy-policy link and deletion control to the commander profile screen.
- Decide whether this is categorized as a Game / Board game, complete the IARC content-rating questionnaire, declare the target audience, ads status, government/news/health/financial applicability, and app access instructions.
- Test offline play, game restoration, QR pairing, QR joining, profile editing, pause/resume, real-time games, and turn-based games on several physical phones and Android versions.
- Add crash/ANR monitoring or establish another production diagnostic process.

## Store listing assets

- App title **Diller Cartarchy**, short description, full description, support email, website, and privacy-policy URL.
- A polished 512×512 Play icon; the current launcher vector is a development placeholder.
- A 1024×500 JPEG or 24-bit PNG feature graphic.
- At least two accurate phone screenshots; 9:16 portrait screenshots at 1080 pixels wide are a good fit for this UI. Avoid claims or third-party branding that cannot be substantiated. [Preview asset requirements](https://support.google.com/googleplay/android-developer/answer/9866151)

## Signing and bundle

1. Generate an upload keystore in a secure location outside the repository. Back it up and keep its passwords in a secrets manager.
2. Configure release signing through environment variables, an untracked properties file, or CI secrets. Never commit the keystore or passwords.
3. Increment `versionCode` for every Play upload and update `versionName` for user-facing releases.
4. Run the complete release gate:

   ```sh
   ./gradlew clean testDebugUnitTest connectedDebugAndroidTest lintRelease bundleRelease
   ```

5. Upload the signed `app-release.aab`, enroll in mandatory Play App Signing, and start with Internal testing. Google Play uses the bundle to generate optimized APKs. [App signing](https://developer.android.com/studio/publish/app-signing) and [bundle upload](https://developer.android.com/studio/publish/upload-bundle)
6. Resolve every pre-launch report, policy, device-compatibility, and Data safety warning before closed testing and production rollout.

Do not generate the production upload key casually: losing it creates an account-recovery process, while leaking it compromises the release pipeline.
