#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:-}"
source_root="${MILKBAR_CEMU_SOURCE:-$root/.tools/Cemu}"
display_backend="native"

case "$target" in
  mac_64)
    expected_os="Darwin"; expected_arch="x86_64"; cmake_arch="x86_64"
    platform_flags=(-DMACOS_BUNDLE=ON -DENABLE_METAL=OFF -DENABLE_VULKAN=ON)
    ;;
  mac_aarch64)
    expected_os="Darwin"; expected_arch="arm64"; cmake_arch="arm64"
    # Current Cemu shares a few shader-stage definitions with its Vulkan
    # sources, so keep Vulkan compiled while selecting Metal at runtime.
    platform_flags=(-DMACOS_BUNDLE=ON -DENABLE_METAL=ON -DENABLE_VULKAN=ON -DENABLE_OPENGL=OFF)
    ;;
  linux_64)
    expected_os="Linux"; expected_arch="x86_64"; cmake_arch=""
    platform_flags=()
    ;;
  linux_aarch64)
    expected_os="Linux"; expected_arch="arm64"; cmake_arch=""
    platform_flags=()
    ;;
  *)
    echo "Usage: $0 {mac_64|mac_aarch64|linux_64|linux_aarch64}" >&2
    exit 2
    ;;
esac

host_os="$(uname -s)"
host_arch="$(uname -m)"
[[ "$host_arch" == "aarch64" ]] && host_arch="arm64"
if [[ "$host_os" != "$expected_os" || "$host_arch" != "$expected_arch" ]]; then
  echo "$target must be built on $expected_os/$expected_arch; this host is $host_os/$host_arch." >&2
  exit 1
fi

if [[ "$host_os" == "Darwin" ]]; then
  command -v brew >/dev/null || {
    echo "Homebrew is required to build Cemu on macOS: https://brew.sh" >&2
    exit 1
  }
  missing_formulae=()
  for formula in automake boost cmake git libtool nasm ninja pkgconf; do
    brew list --versions "$formula" >/dev/null 2>&1 || missing_formulae+=("$formula")
  done
  if (( ${#missing_formulae[@]} )); then
    brew install "${missing_formulae[@]}"
  fi
fi

for tool in git cmake ninja; do
  command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 1; }
done

if [[ "$host_os" == "Linux" ]]; then
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists 'wayland-protocols >= 1.15'; then
    platform_flags=(-DENABLE_WAYLAND=ON)
    display_backend="wayland-x11"
    echo "Cemu Linux backend: Wayland and X11 (wayland-protocols detected)."
  else
    platform_flags=(-DENABLE_WAYLAND=OFF)
    display_backend="x11"
    echo "Cemu Linux backend: X11/XWayland (wayland-protocols not installed)."
  fi
fi

if [[ ! -d "$source_root/.git" ]]; then
  mkdir -p "$(dirname "$source_root")"
  git clone --recursive https://github.com/cemu-project/Cemu.git "$source_root"
else
  git -C "$source_root" submodule update --init --recursive
fi

"$root/scripts/patch-cemu.sh" "$source_root"
if [[ "$target" == "mac_aarch64" ]]; then
  build_root="$source_root/build-milkbar-arm64-metal"
else
  build_root="$source_root/build-milkbar-$target"
fi
cmake_args=(-S "$source_root" -B "$build_root" -G Ninja -DCMAKE_BUILD_TYPE=Release)
if [[ -n "$cmake_arch" ]]; then
  cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=$cmake_arch")
fi
cmake "${cmake_args[@]}" "${platform_flags[@]}"
cmake --build "$build_root" --parallel

if [[ -n "${MILKBAR_CEMU_INSTALL_DIR:-}" ]]; then
  install_root="$MILKBAR_CEMU_INSTALL_DIR"
elif [[ "$host_os" == "Darwin" ]]; then
  install_root="$HOME/Library/Application Support/MilkBarLauncher/runtimes/cemu/$target"
else
  install_root="${XDG_DATA_HOME:-$HOME/.local/share}/MilkBarLauncher/runtimes/cemu/$target"
fi
mkdir -p "$install_root"

commit="$(git -C "$source_root" rev-parse HEAD)"
if [[ "$host_os" == "Darwin" ]]; then
  app="$(find "$source_root/bin" -maxdepth 1 -name 'Cemu_*.app' -print -quit)"
  [[ -n "$app" ]] || { echo "Cemu app bundle was not produced." >&2; exit 1; }
  ditto "$app" "$install_root/Cemu.app"
  codesign --force --deep --sign - "$install_root/Cemu.app"
  executable="$install_root/Cemu.app/Contents/MacOS/$(basename "$app" .app)"
else
  executable="$(find "$source_root/bin" -maxdepth 1 -type f -name 'Cemu_*' -perm -111 -print -quit)"
  [[ -n "$executable" ]] || { echo "Cemu executable was not produced." >&2; exit 1; }
  cp "$executable" "$install_root/Cemu"
  cp -R "$source_root/bin/gameProfiles" "$source_root/bin/resources" "$install_root/"
  executable="$install_root/Cemu"
fi

if [[ "$host_os" == "Darwin" ]]; then
  dynamic_symbols="$(nm -gU "$executable")"
else
  dynamic_symbols="$(nm -D --defined-only "$executable")"
fi
for symbol in memory_getBase osLib_registerHLEFunction milkbar_isHLEReady milkbar_markHooksReady milkbar_isTitleActive; do
  if ! grep -Eq "[[:space:]_]${symbol}$" <<<"$dynamic_symbols"; then
    echo "Patched Cemu is missing required exported symbol: $symbol" >&2
    exit 1
  fi
done

printf '{\n  "target": "%s",\n  "commit": "%s",\n  "executable": "%s",\n  "display_backend": "%s"\n}\n' \
  "$target" "$commit" "$executable" "$display_backend" > "$install_root/runtime.json"
echo "Installed patched Cemu: $executable"
