#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
ndk="${ANDROID_NDK_HOME:-$sdk/ndk/27.3.13750724}"
[[ -f "$ndk/build/cmake/android.toolchain.cmake" ]] || { echo "Set ANDROID_NDK_HOME to an installed Android NDK (r27 or newer)." >&2; exit 1; }
cmake -S "$root/DLL/InjectDLL" -B "$root/Build/android-client" \
  -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/Build/android-client" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
