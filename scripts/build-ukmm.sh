#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:-}"
source_root="${MILKBAR_UKMM_SOURCE:-$root/.tools/UKMM}"

case "$target" in
  mac_64) expected_os="Darwin"; expected_arch="x86_64" ;;
  mac_aarch64) expected_os="Darwin"; expected_arch="arm64" ;;
  linux_64) expected_os="Linux"; expected_arch="x86_64" ;;
  linux_aarch64) expected_os="Linux"; expected_arch="arm64" ;;
  *) echo "Usage: $0 {mac_64|mac_aarch64|linux_64|linux_aarch64}" >&2; exit 2 ;;
esac

host_arch="$(uname -m)"
[[ "$host_arch" == "aarch64" ]] && host_arch="arm64"
if [[ "$(uname -s)" != "$expected_os" || "$host_arch" != "$expected_arch" ]]; then
  echo "$target must be built on $expected_os/$expected_arch." >&2
  exit 1
fi
command -v cargo >/dev/null || { echo "Rust/Cargo is required to build the bundled mod merger." >&2; exit 1; }

if [[ ! -d "$source_root/.git" ]]; then
  mkdir -p "$(dirname "$source_root")"
  git clone --depth 1 https://github.com/NiceneNerd/UKMM.git "$source_root"
fi
"$root/scripts/patch-ukmm.sh" "$source_root"
cargo build --manifest-path "$source_root/Cargo.toml" --release

output="$root/Build/ukmm/$target"
mkdir -p "$output"
cp "$source_root/target/release/ukmm" "$output/ukmm"
cp "$source_root/LICENSE" "$output/UKMM-LICENSE"
echo "Built UKMM merger: $output/ukmm"
