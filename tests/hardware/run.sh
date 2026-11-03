#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace="$(cd "$test_dir/../../.." && pwd)"
build_dir="$(mktemp -d /tmp/velaride-hardware-test.XXXXXX)"
python3 "$test_dir/extract.py" "$workspace" "$build_dir"
for name in i2c adc ltr303 mmc5603 arch_timer; do
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$build_dir/${name}_test.c" -o "$build_dir/${name}_test"
  "$build_dir/${name}_test"
done
cc -D_GNU_SOURCE -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$test_dir/../ride_session/stubs" "$test_dir/device_test.c" \
  -pthread -o "$build_dir/device_test"
"$build_dir/device_test"
cc -D_GNU_SOURCE -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$test_dir/../ride_session/stubs" "$test_dir/compass_test.c" \
  -pthread -lm -o "$build_dir/compass_test"
"$build_dir/compass_test"
echo "Host test build: $build_dir"
