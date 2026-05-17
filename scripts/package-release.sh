#!/bin/sh

set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <version>" >&2
  exit 1
fi

version="$1"
root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root_dir"

os_name=$(uname -s)
arch_name=$(uname -m)
platform_slug=$(printf '%s' "$os_name" | tr '[:upper:]' '[:lower:]')
archive_basename="ember-term-${version}-${platform_slug}-${arch_name}"
stage_dir="${root_dir}/dist/${archive_basename}"
archive_path="${root_dir}/dist/${archive_basename}.tar.gz"
binary_asset_name="ember-term-${platform_slug}-${arch_name}"

rm -rf "${root_dir}/dist"
mkdir -p "$stage_dir"

make clean
make

cp ember-term "$stage_dir/"
cp README.md LICENSE "$stage_dir/"

if [ "$os_name" = "Darwin" ] && [ -n "${SIGNING_IDENTITY:-}" ]; then
  codesign --force --sign "$SIGNING_IDENTITY" --options runtime --timestamp "$stage_dir/ember-term"
  codesign --verify --verbose "$stage_dir/ember-term"
fi

cp "$stage_dir/ember-term" "${root_dir}/dist/${binary_asset_name}"
tar -czf "$archive_path" -C "${root_dir}/dist" "$archive_basename"
printf '%s\n' "${root_dir}/dist/${binary_asset_name}"
printf '%s\n' "$archive_path"
