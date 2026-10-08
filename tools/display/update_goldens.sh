#!/bin/sh
# Rewrite the screen test's golden images (tests/display/screens/golden)
# from the current drawing code, and PNGs of them for review.
#
#   tools/display/update_goldens.sh [build dir]
#
# Review every changed image (git diff shows which) before committing:
# the goldens are what the screens test holds the code to.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:-"$root/../build-goldens"}
west build --no-sysbuild -b native_sim "$root/tests/display/screens" -d "$build" -p auto -- -DGOLDEN_UPDATE=1
"$build/zephyr/zephyr.exe" >/dev/null || true
python3 "$root/tools/display/render_png.py" "$root/tests/display/screens/golden" "$root/docs/images/screens"
git -C "$root" status --short tests/display/screens/golden docs/images/screens
