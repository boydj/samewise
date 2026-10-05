#!/usr/bin/env bash
# Run the XIAO image in Renode and write the bench report.
#
#   tests/renode/run.sh <renode dir> <build dir> <out dir> [native_sim console log]
#
# <build dir> is a xiao_ble/nrf52840 build of app/ (zephyr.elf, the clip's
# expected lines, size.json from tools/size). The platform is Renode's
# nRF52840 with Nordic's SVD from the workspace instead of a download.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "${here}/../.." && pwd)"
renode="$(cd "$1" && pwd)"
build="$(cd "$2" && pwd)"
mkdir -p "$3"
out="$(cd "$3" && pwd)"
native="${4:-}"

svd="$(find "${repo}/../modules/hal/nordic" -name nrf52840.svd | head -1)"
[ -n "${svd}" ] || { echo "nrf52840.svd not found in the west workspace" >&2; exit 1; }
sed "s|ApplySVD @https://[^ ]*|ApplySVD @${svd}|" \
    "${renode}/platforms/cpus/nrf52840.repl" > "${out}/nrf52840.repl"
rm -f "${out}/uart.log" "${out}/instructions.txt"

"${renode}/renode-test" "${here}/xiao.robot" \
    --variable "ELF:${build}/app/zephyr/zephyr.elf" \
    --variable "PLATFORM:${out}/nrf52840.repl" \
    --variable "OUT:${out}" \
    --results-dir "${out}/robot"

python3 "${repo}/tools/renode/bench.py" \
    --uart "${out}/uart.log" \
    --instructions "${out}/instructions.txt" \
    --expected "${build}/app/wx_clip/wx_clip.txt" \
    --size "${build}/size.json" \
    ${native:+--native "${native}"} \
    --markdown "${out}/xiao-bench.md" \
    --json "${out}/xiao-bench.json"
