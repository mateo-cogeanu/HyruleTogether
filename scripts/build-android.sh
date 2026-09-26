#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
source="${HYRULE_ANDROID_SOURCE:-$root/.tools/CemuAndroid}"
revision="$(cat "$root/Android/cemu-revision.txt")"
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
export HYRULE_NDK_VERSION="${HYRULE_NDK_VERSION:-27.3.13750724}"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/$HYRULE_NDK_VERSION}"
if [[ -z "${JAVA_HOME:-}" && -d "/Applications/Android Studio.app/Contents/jbr/Contents/Home" ]]; then
  export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
fi
if [[ ! -d "$source/.git" ]]; then
  git clone https://github.com/SSimco/Cemu.git "$source"
  git -C "$source" checkout --detach "$revision"
fi
[[ "$(git -C "$source" rev-parse HEAD)" == "$revision" ]] || { echo "Cemu revision differs from Android/cemu-revision.txt" >&2; exit 1; }
git -C "$source" submodule update --init --recursive
"$root/scripts/build-android-client.sh"
python3 "$root/scripts/prepare-android.py" "$source"
(cd "$source/src/android" && ./gradlew --no-daemon :app:assembleDebug)
mkdir -p "$root/Build/android"
cp "$source/src/android/app/build/outputs/apk/debug/app-debug.apk" "$root/Build/android/HyruleTogether-debug.apk"
echo "APK: $root/Build/android/HyruleTogether-debug.apk"
