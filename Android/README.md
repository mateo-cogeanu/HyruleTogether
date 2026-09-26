# Hyrule Together Android client

Client-only ARM64 integration targeting the OnePlus 13 running LineageOS 23.2
(Android 16). The existing desktop dedicated server remains unchanged.

This is an experimental port, not yet validated on a physical phone. It uses
[SSimco/Cemu's Android port](https://github.com/SSimco/Cemu), pinned by
`cemu-revision.txt`. The build keeps that emulator in the ignored `.tools/`
directory and applies the reproducible `cemu-hooks.patch` and `overlay/` files.
The APK has its own application ID and does not replace a normal Cemu install.

## Build

Install Android Studio, SDK platform 36, and NDK 27.3.13750724, plus CMake,
Git, Python 3, and a JDK supported by Gradle 9.3.1. On macOS the build uses
Android Studio's bundled JDK when `JAVA_HOME` is unset.

```sh
./scripts/build-android.sh
```

Output: `Build/android/HyruleTogether-debug.apk`. This is a debug-signed test
build. Build just the multiplayer library with `scripts/build-android-client.sh`.
The first complete emulator build also downloads and compiles vcpkg dependencies.
Set `ANDROID_HOME`, `ANDROID_NDK_HOME`, `HYRULE_NDK_VERSION`, or `JAVA_HOME`
when using different installation locations. `HYRULE_ANDROID_SOURCE` can select
an existing checkout at the exact pinned revision.

To run the JNI smoke tests on a connected Android device or ARM64 emulator:

```sh
cd .tools/CemuAndroid/src/android
./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=info.cemu.cemu.HyruleSessionTest
```

The instrumentation suite covers launcher navigation, setup diagnostics, server
validation and persistence, native library loading, native connection validation,
title/region/version validation, and synthetic game/update/DLC imports including
rollback after a read failure. These tests do not establish BOTW playability.

## Set up directly on Android

Install `Build/android/HyruleTogether-debug.apk`, then open Hyrule Together.
The Android launcher follows the desktop Settings / Lobby Browser / Model
Selection layout, using the existing backgrounds, logo, and character art.
It uses native Android controls and adapts to portrait and landscape displays.

1. In **Settings**, enter your player name and choose **Browse — Add BOTW**.
   Select your decrypted base-game folder containing `code`, `content`, and
   `meta/meta.xml` (including `code/U-King.rpx`). The app copies the game into
   its own Cemu storage; you do not need a permanent grant to the original folder.
2. Choose **Install BOTW Update** and select the matching v208 update folder.
   **Install BOTW DLC** accepts the matching region's DLC folder. Incorrect
   titles, regions, and update versions are rejected before copying.
3. Choose **Import Prepared Multiplayer Packs** and select your existing desktop
   `graphicPacks` folder containing `BreathOfTheWild_UKMM` and
   `downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory`.
   This imports the prepared mod through the app; ADB is not required.
4. **Run Setup Check** checks your installed files and player settings.
   **Manage Cemu Graphic Packs** and **Controller & Emulator Settings** open the
   corresponding emulator settings.
5. In **Lobby Browser**, add/edit a saved server or use **Direct IP**. Select the
   server and connect. Passwords remain in memory for the current app process.
6. **Model Selection** saves your character choice and passes it to the native
   multiplayer client when you connect.

Imports report byte progress, allow cancellation, check free space, and stage
files before replacing a title. The old title remains intact if a copy fails.
An in-progress import survives activity rotation, but is not resumed after
Android kills the process. Keep the app open until installation completes.

**Remaining desktop parity gap:** automatic on-device UKMM merging and player
model generation are not implemented. Multiplayer still requires packs prepared
by the desktop launcher, matching your game region/update/DLC. The Android UI
and game importer do not remove that requirement. Encrypted dumps and archive
imports are not supported by this folder-based setup flow.

For development only, `scripts/install-android-test.sh` remains an optional
ADB installation/pack-copy helper. Set `ANDROID_SERIAL` with multiple devices.

Validated on 2026-09-26: debug APK build and seven ARM64 Android 16 instrumentation
tests passed. Initial integration also passed native/APK 16 KiB alignment checks
and a macOS universal native-client rebuild. No physical-device game,
graphics-driver, or multiplayer-session test has run.

Android storage: `/sdcard/Android/data/app.hyruletogether.android.debug/files`.
Native multiplayer logs are in private app storage; on this debug build read them
with `adb shell run-as app.hyruletogether.android.debug cat files/hyrule/LatestLog.txt`.
Connection transitions also appear as toasts and in `adb logcat -s HyruleTogether`.

## Implementation and validation boundaries

- The C++ client uses the Android NDK, Android's libc thread support, and 16 KiB
  ELF page alignment. Cemu and its graphics-driver helper libraries receive the
  same alignment flag.
- Cemu exports its memory base, HLE registration, readiness, and title lifetime
  hooks; custom HLE-only game modules are accepted.
- The app uses `dlopen` in its own process and a private socketpair to run the
  existing connect/start handshake. No root, external injection, or local server
  is required. Native workers start only when the bridge supplies IPC settings.
- Android's existing Cemu surface pause/resume handling is retained. Native game
  workers observe the title lifetime guard before accessing emulated memory.
- A failed connection requires exiting/restarting the app before retrying. This
  prototype does not reset the legacy client's global state in place.
- An Android build does not establish BOTW playability or multiplayer correctness.
  Physical-device checks must cover game startup, background/resume, disconnect,
  remote spawning/movement, equipment switching, arrows, and sustained performance.
- The launcher is an Android implementation of the desktop design, not a bundled
  Python/Qt runtime. Automatic mod merging remains a desktop parity gap; Android
  server hosting remains outside the requested client scope.
