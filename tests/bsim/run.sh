#!/usr/bin/env bash
# Build the nrf52_bsim test images and run every test script under tests/bsim.
#
# Needs ZEPHYR_BASE, BSIM_OUT_PATH and BSIM_COMPONENTS_PATH (see CLAUDE.md).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${BSIM_OUT_PATH:?BSIM_OUT_PATH must point at a built BabbleSim}"
: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set}"
board=nrf52_bsim/native
build="${WX_BSIM_BUILD:-${here}/../../build-bsim}"

for test in "${here}"/*/; do
  name="$(basename "${test}")"
  west build -b "${board}" "${test}" -d "${build}/${name}" -p auto
  # sysbuild puts the image in a directory named after the test
  cp "${build}/${name}/${name}/zephyr/zephyr.exe" \
     "${BSIM_OUT_PATH}/bin/bs_${board//\//_}_wx_${name}"
done

status=0
for script in "${here}"/*/test_scripts/*.sh; do
  echo "== ${script#${here}/}"
  "${script}" || status=1
done
exit ${status}
