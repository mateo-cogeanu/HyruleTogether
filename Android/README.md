# Hyrule Together Android proof of concept

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

These four checks exercise native-library loading, connection validation,
rejection of unrecognized games, and launch of the connection dialog. They do
not simulate BOTW or a multiplayer session.

Validated on 2026-09-26: the debug APK built successfully and all four tests
passed on an ARM64 Android 16 emulator. The installation helper installed and
opened the app and copied the existing prepared packs. All packaged native
libraries passed the 16 KiB ELF alignment check, and the APK passed
`zipalign -c -P 16 4`. The macOS universal native client also rebuilt successfully.
No physical-device game, graphics-driver, or multiplayer-session test has run.

## Device setup

Enable USB debugging, connect and authorize the phone, and check `adb devices`.
Then run:

```sh
./scripts/install-android-test.sh
# Or provide an APK and your existing desktop graphicPacks directory:
./scripts/install-android-test.sh /path/to/test.apk /path/to/graphicPacks
```

For multiple devices set `ANDROID_SERIAL`. This helper installs the APK and
copies only your already-merged `BreathOfTheWild_UKMM` and Extended Memory packs
into the app's external-files directory. It does not upload them anywhere or
include them in the APK. Keep base game, update, DLC, and the merged mod region
consistent; the current client/mod targets BOTW v208. Install your own game data
through the emulator's title manager. Mod merging on Android is not implemented.

Open Hyrule Together, enter the existing desktop server's LAN address and port
(default 5050), choose a distinct player name, and enable multiplayer. Passwords
are kept only for the current app process. The connection dialog appears when
opening the main activity; launching a recognized BOTW title starts the client.
Required installed packs are enabled automatically. Turning multiplayer off
removes the multiplayer pack from the active set.

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
- The test APK uses Cemu's existing interface plus the connection dialog. A final
  mobile launcher, automatic mod merging, and an Android server are outside this
  first integration.
