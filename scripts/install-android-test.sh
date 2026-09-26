#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
apk="${1:-$root/Build/android/HyruleTogether-debug.apk}"
packs="${2:-$HOME/Library/Application Support/MilkBarLauncher/cemu/config/graphicPacks}"
adb_run() {
  if [[ -n "${ANDROID_SERIAL:-}" ]]; then adb -s "$ANDROID_SERIAL" "$@"; else adb "$@"; fi
}
# Use ANDROID_SERIAL explicitly when more than one phone/emulator is attached.
adb_run get-state >/dev/null
[[ -f "$apk" ]] || { echo "Build the APK first: ./scripts/build-android.sh" >&2; exit 1; }
[[ -f "$packs/BreathOfTheWild_UKMM/content/Pack/TitleBG.pack" ]] || { echo "Pass your prepared desktop graphicPacks directory as argument 2." >&2; exit 1; }
extended="downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory"
[[ -f "$packs/$extended/rules.txt" ]] || { echo "Extended Memory pack is missing." >&2; exit 1; }
package="app.hyruletogether.android.debug"
adb_run install -r "$apk"
adb_run shell am start -n "$package/info.cemu.cemu.HyruleLauncherActivity"
adb_run shell am force-stop "$package"
destination="/sdcard/Android/data/$package/files/graphicPacks"
adb_run shell mkdir -p "$destination/downloadedGraphicPacks/BreathOfTheWild/Mods"
adb_run push "$packs/BreathOfTheWild_UKMM" "$destination/"
adb_run push "$packs/$extended" "$destination/downloadedGraphicPacks/BreathOfTheWild/Mods/"
adb_run shell am start -n "$package/info.cemu.cemu.HyruleLauncherActivity"
echo "Installed client and prepared packs. Select your own BOTW base game and install v208 in the app."
