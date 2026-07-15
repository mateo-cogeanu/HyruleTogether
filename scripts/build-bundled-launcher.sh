#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:-}"

case "$target" in
  mac_x86_64) server_rid="osx-x64"; expected_os="Darwin"; expected_arch="x86_64"; client_ext="dylib" ;;
  mac_arm64_Metal) server_rid="osx-arm64"; expected_os="Darwin"; expected_arch="arm64"; client_ext="dylib" ;;
  Linux_x86_64) server_rid="linux-x64"; expected_os="Linux"; expected_arch="x86_64"; client_ext="so" ;;
  Linux_arm64) server_rid="linux-arm64"; expected_os="Linux"; expected_arch="arm64"; client_ext="so" ;;
  *) echo "Usage: $0 {mac_x86_64|mac_arm64_Metal|Linux_x86_64|Linux_arm64}" >&2; exit 2 ;;
esac

host_os="$(uname -s)"
host_arch="$(uname -m)"
[[ "$host_arch" == "aarch64" ]] && host_arch="arm64"
if [[ "$host_os" != "$expected_os" || "$host_arch" != "$expected_arch" ]]; then
  echo "$target must be compiled on $expected_os/$expected_arch; this host is $host_os/$host_arch." >&2
  exit 1
fi

python="$root/.venv/bin/python"
if [[ ! -x "$python" ]]; then
  echo "Creating the Hyrule Together build environment..."
  python3 -m venv "$root/.venv"
fi
"$python" -m pip install --upgrade pip
"$python" -m pip install -r "$root/CrossPlatform/requirements.txt"

packager="portable-python"
if "$python" -m PyInstaller --version >/dev/null 2>&1; then
  packager="pyinstaller"
elif [[ "$host_os" != "Darwin" ]]; then
  echo "PyInstaller is required for Linux bundles. Install CrossPlatform/requirements.txt." >&2
  exit 1
fi

"$root/scripts/build-native.sh"
"$root/scripts/build-ukmm.sh" "$target"
"$root/scripts/build-model-builder.sh" "$server_rid"
if [[ ! -x "$root/Build/server/$server_rid/MBL.DedicatedServer" ]]; then
  "$root/scripts/build-server.sh" "$server_rid"
fi

build_root="$root/Build/launcher/$target"
staging="$build_root/staging"
rm -rf "$staging" "$build_root/dist" "$build_root/work"
mkdir -p "$staging/runtime/cemu" "$staging/runtime/client" "$staging/runtime/server" "$staging/runtime/mod" "$staging/runtime/tools"
MILKBAR_CEMU_INSTALL_DIR="$staging/runtime/cemu" "$root/scripts/install-patched-cemu.sh" "$target"
client_source="$root/Build/cross-platform/libMilkBarClient.$client_ext"
client_destination="$staging/runtime/client/libMilkBarClient.$client_ext"
if [[ "$host_os" == "Darwin" ]]; then
  # Development builds remain universal so either local Cemu architecture can
  # preload them, but distributable launchers contain only their named target.
  lipo "$client_source" -thin "$expected_arch" -output "$client_destination"
else
  cp "$client_source" "$client_destination"
fi
cp -R "$root/Build/server/$server_rid/." "$staging/runtime/server/"
rm -rf "$staging/runtime/server/Logs" "$staging/runtime/server/LatestLog.txt"
cp "$root/BNP Files/MilkBarLauncher.bnp" "$staging/runtime/mod/"
(cd "$staging/runtime/mod" && cmake -E tar xvf MilkBarLauncher.bnp >/dev/null)
cp "$root/CrossPlatform/patch_BeltLookupGuard.asm" "$staging/runtime/mod/patches/"
"$python" "$root/scripts/patch-spawn-owner.py" "$staging/runtime/mod/patches/patch_SpawnActors.asm"
"$python" "$root/scripts/patch-equipment-factory.py" "$staging/runtime/mod/patches/patch_SpawnActors.asm"
"$python" "$root/scripts/patch-actor-delete.py" "$staging/runtime/mod/patches/patch_UKL_ActorInterceptor.asm"
rm -rf "$staging/runtime/mod/content" "$staging/runtime/mod/logs" "$staging/runtime/mod/info.json"
cp "$root/Build/ukmm/$target/ukmm" "$root/Build/ukmm/$target/UKMM-LICENSE" "$staging/runtime/tools/"
cp "$root/Build/model-builder/$server_rid/milkbar-model-builder" "$staging/runtime/tools/"

if [[ "$packager" == "pyinstaller" ]]; then
pyinstaller_args=(
  --noconfirm --clean --windowed --onedir
  --name "Hyrule Together"
  --icon "$root/CrossPlatform/icon.png"
  --distpath "$build_root/dist"
  --workpath "$build_root/work"
  --specpath "$build_root"
  --paths "$root/CrossPlatform"
  --add-data "$root/WPF .NET 6/Breath of the Wild Multiplayer/Images:WPF .NET 6/Breath of the Wild Multiplayer/Images"
  --add-data "$root/WPF .NET 6/Breath of the Wild Multiplayer/Backgrounds:WPF .NET 6/Breath of the Wild Multiplayer/Backgrounds"
  --add-data "$root/WPF .NET 6/Breath of the Wild Multiplayer/Resources:WPF .NET 6/Breath of the Wild Multiplayer/Resources"
  --add-data "$root/WPF .NET 6/Breath of the Wild Multiplayer/AppdataFiles:WPF .NET 6/Breath of the Wild Multiplayer/AppdataFiles"
  --add-data "$root/CrossPlatform/icon.png:."
  "$root/CrossPlatform/milkbar_qt_gui.py"
)
"$python" -m PyInstaller "${pyinstaller_args[@]}"
else
  app="$build_root/dist/Hyrule Together.app"
  contents="$app/Contents"
  frameworks="$contents/Frameworks"
  resources="$contents/Resources"
  site_packages="$resources/python"
  mkdir -p "$contents/MacOS" "$frameworks" "$resources/app" "$site_packages/PySide6/Qt/lib" "$site_packages/PySide6/Qt/plugins"
  clang -Os -mmacosx-version-min=13.4 "$root/CrossPlatform/macos_bundle_launcher.c" -o "$contents/MacOS/Hyrule Together"
  cp "$root/CrossPlatform/MilkBarLauncher-Info.plist" "$contents/Info.plist"

  py_version="$($python -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
  python_framework="$($python -c 'import sysconfig; print(sysconfig.get_config_var("PYTHONFRAMEWORKPREFIX") + "/Python.framework")')"
  ditto "$python_framework" "$frameworks/Python.framework"
  # Homebrew's framework includes a site-packages symlink that points outside
  # the copied framework. The launcher supplies its own bundled site-packages,
  # and leaving this dead symlink makes strict deep signature validation fail.
  rm -f "$frameworks/Python.framework/Versions/$py_version/lib/python$py_version/site-packages"
  python_binary="$frameworks/Python.framework/Versions/$py_version/bin/python$py_version"
  old_python="$(otool -L "$python_binary" | sed -n '2s/^[[:space:]]*\([^[:space:]]*\).*/\1/p')"
  install_name_tool -change "$old_python" "@executable_path/../Python" "$python_binary"
  codesign --force --sign - "$python_binary"
  codesign --force --sign - "$frameworks/Python.framework"

  source_packages="$root/.venv/lib/python$py_version/site-packages"
  for file in __init__.py _config.py QtCore.abi3.so QtGui.abi3.so QtWidgets.abi3.so libpyside6.abi3.6.11.dylib; do
    cp "$source_packages/PySide6/$file" "$site_packages/PySide6/"
  done
  cp -R "$source_packages/PySide6/support" "$site_packages/PySide6/"
  cp -R "$source_packages/shiboken6" "$site_packages/"
  for framework in QtCore QtDBus QtGui QtNetwork QtPdf QtSvg QtWidgets; do
    ditto "$source_packages/PySide6/Qt/lib/$framework.framework" "$site_packages/PySide6/Qt/lib/$framework.framework"
  done
  for plugin in platforms imageformats styles iconengines; do
    cp -R "$source_packages/PySide6/Qt/plugins/$plugin" "$site_packages/PySide6/Qt/plugins/"
  done

  cp "$root/CrossPlatform/milkbar_launcher.py" "$root/CrossPlatform/milkbar_qt_gui.py" "$resources/app/"
  cp "$root/CrossPlatform/icon.png" "$resources/app/icon.png"
  cp "$root/CrossPlatform/icon.png" "$resources/icon.png"
  mkdir -p "$resources/WPF .NET 6/Breath of the Wild Multiplayer"
  for asset in Images Backgrounds Resources AppdataFiles; do
    cp -R "$root/WPF .NET 6/Breath of the Wild Multiplayer/$asset" "$resources/WPF .NET 6/Breath of the Wild Multiplayer/"
  done
fi

if [[ "$host_os" == "Darwin" ]]; then
  app="$build_root/dist/Hyrule Together.app"
  runtime="$app/Contents/Resources/runtime"
  mkdir -p "$runtime"
  ditto "$staging/runtime" "$runtime"
  codesign --force --deep --sign - "$app"
  ditto -c -k --sequesterRsrc --keepParent "$app" "$build_root/HyruleTogether-$target.zip"
  echo "Bundled launcher: $app"
else
  package="$build_root/dist/Hyrule Together"
  cp -R "$staging/runtime" "$package/runtime"
  tar -C "$build_root/dist" -czf "$build_root/HyruleTogether-$target.tar.gz" "Hyrule Together"
  echo "Bundled launcher: $package"
fi
