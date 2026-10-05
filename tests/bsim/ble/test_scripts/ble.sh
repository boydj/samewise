#!/usr/bin/env bash
# Bluetooth settings on nrf52_bsim: the radio and three phones.
#
# Needs BSIM_OUT_PATH and BSIM_COMPONENTS_PATH (BabbleSim built), ZEPHYR_BASE,
# and the image built and copied by tests/bsim/run.sh.

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="wx_ble"
verbosity_level=2
EXECUTE_TIMEOUT=300
exe=./bs_${BOARD_TS}_wx_ble

cd ${BSIM_OUT_PATH}/bin

Execute $exe -v=${verbosity_level} -s=${simulation_id} -d=0 -testid=radio -RealEncryption=1
Execute $exe -v=${verbosity_level} -s=${simulation_id} -d=1 -testid=phone1 -RealEncryption=1
Execute $exe -v=${verbosity_level} -s=${simulation_id} -d=2 -testid=phone2 -RealEncryption=1
Execute $exe -v=${verbosity_level} -s=${simulation_id} -d=3 -testid=phone3 -RealEncryption=1

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} -D=4 -sim_length=900e6 $@

wait_for_background_jobs
