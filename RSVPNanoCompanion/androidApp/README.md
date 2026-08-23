# RSVP Nano Android Companion

Native Android companion app for RSVP Nano.
Requires Android 14 or newer.

The Android UI is Jetpack Compose. Business logic comes from Kotlin Multiplatform modules:
`:shared` owns companion workflows, persistence, RSS/article drafts, device API access, and sync
orchestration; `:conversionCore` owns document conversion.

## Build

Install Android Studio or a JDK 17 + Android SDK environment, then run from the repository root:

```bash
./gradlew checkAndroid
```

The release APK is written under:

```text
RSVPNanoCompanion/androidApp/build/outputs/apk/release/
```

## Run

1. Open the repository root in Android Studio.
2. Select the `androidApp` run configuration.
3. Run on an emulator or Android device.

Or install the debug APK with Android SDK platform tools:

```bash
adb install -r RSVPNanoCompanion/androidApp/build/outputs/apk/debug/androidApp-debug.apk
adb shell monkey -p com.rsvpnano.android -c android.intent.category.LAUNCHER 1
```

If multiple Android devices/emulators are connected, pass the ADB serial:

```bash
adb devices
adb -s SERIAL_FROM_ADB install -r RSVPNanoCompanion/androidApp/build/outputs/apk/debug/androidApp-debug.apk
adb -s SERIAL_FROM_ADB shell monkey -p com.rsvpnano.android -c android.intent.category.LAUNCHER 1
```

## Wireless Device Install

ADB-over-Wi-Fi works, but Android's Wireless debugging flow usually requires pairing first from
Developer options.

```bash
adb pair PHONE_LAN_IP:PAIRING_PORT
adb connect PHONE_LAN_IP:DEBUGGING_PORT
adb -s PHONE_LAN_IP:DEBUGGING_PORT install -r RSVPNanoCompanion/androidApp/build/outputs/apk/debug/androidApp-debug.apk
```

If the phone was first connected by USB and TCP mode is enabled:

```bash
adb -s SERIAL_FROM_ADB tcpip 5555
adb connect PHONE_LAN_IP:5555
adb -s PHONE_LAN_IP:5555 install -r RSVPNanoCompanion/androidApp/build/outputs/apk/debug/androidApp-debug.apk
```

## Connect To The Reader

1. Put the reader into `Companion sync`.
2. Press `Connect` in the app.
3. If the reader joined the same Wi-Fi network, the app discovers it automatically.
4. Otherwise, Android offers the reader's `RSVP-Nano-xxxxxx` direct network.

The app permits cleartext HTTP because the reader exposes its companion API only on the local
network or its direct Wi-Fi network while Companion Sync is open.

Connecting performs one identity request to `/api/v2/device`. Library and settings resources load
when their screens need them, and a failed resource request does not discard an otherwise healthy
reader connection. On Android, direct-network requests use sockets bound to the selected Nano
network; the app does not switch the process-wide network used by catalog downloads and article
fetching.

## Share Target

The Android share target accepts:

- URLs.
- Plain text.
- Text-like files: `.txt`, `.md`, `.markdown`, `.html`, `.htm`, `.xhtml`.

Shared URLs/text are saved as local article drafts through shared import preparation. Text-like
files are read and saved as local drafts. Open the app later, connect to the reader, then sync saved
articles.

## Current Capabilities

The app can list/upload/delete books, read and save device settings, read/save/clear Wi-Fi
settings, add local RSS feeds, sync RSS feeds, save article drafts, fetch URL-only article drafts,
sync saved articles, and set a book's saved resume location when indexed metadata is available.
Device operations use the shared `NanoApi`/controller services and should stay thin in the Compose
layer.
