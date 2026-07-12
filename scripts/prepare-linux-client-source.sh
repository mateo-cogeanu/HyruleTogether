#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="$root/DLL/InjectDLL"
output_dir="$root/Build/linux-client-source"

rm -rf "$output_dir"
mkdir -p "$output_dir"
cp -R "$source_dir/." "$output_dir/"

# The original Windows code uses MSVC extensions accepted as warnings by Apple
# Clang but rejected by GCC. Normalize only the generated Linux copy, leaving
# the tested macOS and Windows sources byte-for-byte unchanged.
find "$output_dir" -type f \( -name '*.h' -o -name '*.cpp' \) -exec \
  sed -i -E \
    -e 's/([[:space:]])static class /\1class /g' \
    -e 's/^([[:space:]]*)extern (struct|union) /\1\2 /g' {} +

# GCC does not make C math functions available implicitly like the original
# MSVC build did. Add the standard header to the generated translation units
# that use them and qualify the remaining unqualified tangent call.
for cpp in InterpolationFunctions.cpp CharacterClasses.cpp; do
  if ! grep -q '^#include <cmath>$' "$output_dir/$cpp"; then
    sed -i '1i#include <cmath>' "$output_dir/$cpp"
  fi
done
sed -i -E 's/(^|[^[:alnum:]_:])tan\(/\1std::tan(/g' "$output_dir/CharacterClasses.cpp"

# These are legal but noisy remnants of the old Windows project. Clean them
# only in the generated Linux copy so GCC output highlights real failures.
find "$output_dir" -type f -name '*.cpp' -exec sed -i '/^[[:space:]]*#pragma once[[:space:]]*$/d' {} +
sed -i 's/return NULL;/return 0;/g' "$output_dir/Scanner.cpp"

echo "Prepared GCC-compatible Linux client sources at $output_dir"
