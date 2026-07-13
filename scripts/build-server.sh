#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
dotnet="${DOTNET:-$(command -v dotnet || true)}"

if [[ -z "$dotnet" ]]; then
  dotnet="$root/.tools/dotnet/dotnet"
  if [[ ! -x "$dotnet" ]]; then
    mkdir -p "$root/.tools/dotnet"
    installer="${TMPDIR:-/tmp}/milkbar-dotnet-install.sh"
    curl -fsSL https://dot.net/v1/dotnet-install.sh -o "$installer"
    bash "$installer" --channel 8.0 --install-dir "$root/.tools/dotnet"
  fi
fi

if [[ -n "${1:-}" ]]; then
  rid="$1"
else
  machine="$(uname -m)"
  [[ "$machine" == "x86_64" ]] && architecture="x64" || architecture="arm64"
  [[ "$(uname -s)" == "Darwin" ]] && platform="osx" || platform="linux"
  rid="$platform-$architecture"
fi

output="$root/Build/server/$rid"
restore_arg=""
if [[ "${MILKBAR_DOTNET_NO_RESTORE:-0}" == "1" ]]; then
  restore_arg="--no-restore"
fi
publish_args=(publish "$root/C#/BOTW.DedicatedServer/BOTWM.DedicatedServer.csproj")
if [[ -n "$restore_arg" ]]; then
  publish_args+=("$restore_arg")
fi
"$dotnet" "${publish_args[@]}" \
  -c Release -r "$rid" --self-contained true \
  -p:PublishSingleFile=true -p:DebugType=None -p:DebugSymbols=false \
  -o "$output"

echo "Dedicated server built at $output/MBL.DedicatedServer"
