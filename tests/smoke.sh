#!/bin/sh

set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root_dir"

help_output=$(./ember-term --help 2>&1)
printf '%s\n' "$help_output" | grep -F -- "--mode widget|fullscreen|print-frame" >/dev/null

fire_frame=$(./ember-term --scene fire --palette gruvbox --fps 8 --width 33 --height 12 --mode print-frame)
printf '%s\n' "$fire_frame" | grep -F "/____\\" >/dev/null

spiral_frame=$(./ember-term --scene spiral --chars " .:^*x#%@" --width 33 --height 12 --mode print-frame)
printf '%s\n' "$spiral_frame" | grep -F "^" >/dev/null

benchmark_output=$(./ember-term --scene lava --width 80 --height 24 --benchmark 10)
printf '%s\n' "$benchmark_output" | grep -F "benchmark scene=lava size=80x24 frames=10" >/dev/null

if ./ember-term --mode nope >/dev/null 2>&1; then
  echo "expected invalid mode to fail" >&2
  exit 1
fi

echo "smoke tests passed"
