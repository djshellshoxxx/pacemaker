#!/bin/sh
# Build and run the licensing tests with plain gcc/g++ (no CMake). Run from anywhere.
set -e
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/pm_license_tests
mkdir -p "$OUT"
V=third_party/ed25519
for f in fe ge sc sha512 verify sign keypair; do
  gcc -O2 -c -I$V $V/$f.c -o "$OUT/$f.o"
done
g++ -std=c++17 -O1 -Wall -Wextra -I. License.cpp tests/test_license.cpp "$OUT"/*.o -o "$OUT/test_license"
python3 tools/keygen.py selftest
"$OUT/test_license"
