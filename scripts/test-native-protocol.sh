#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
output="$root/Build/native-protocol-tests"
mkdir -p "$output"
"${CXX:-c++}" -std=c++17 -pthread -Wno-duplicate-decl-specifier \
  -I "$root/DLL/InjectDLL" -I "$root/DLL/InjectDLL/include" \
  "$root/DLL/InjectDLL/tests/QuestProtocol.cpp" \
  "$root/DLL/InjectDLL/Serialization.cpp" -o "$output/quest-protocol"
"$output/quest-protocol"
