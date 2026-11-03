#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_binary="$(mktemp /tmp/velaride-session-test.XXXXXX)"
trap 'rm -f "$test_binary"' EXIT
cc -D_GNU_SOURCE -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$test_dir/stubs" "$test_dir/test.c" -o "$test_binary"
"$test_binary"
