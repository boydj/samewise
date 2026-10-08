/*
 * Si4743 emulator for tests, built from AN332 Rev. 1.2 (sections 4, 5.2
 * to 5.4 and 6.1): commands are one I2C write, replies a read of the
 * status byte and response bytes, CTS when the command is done, ERR on a
 * bad command or argument, and nothing at all (no CTS) for any command
 * but POWER_UP while powered down.
 *
 * Frequencies are in each band's own units: FM 10 kHz, AM 1 kHz, WB 2.5 kHz.
 */

#ifndef EMUL_SI4743_H_
#define EMUL_SI4743_H_

#include <stdbool.h>
#include <stdint.h>

#define EMUL_FUNC_FM 0
#define EMUL_FUNC_AM 1
#define EMUL_FUNC_WB 3

/** Powered down, no stations, no RDS, part number 0x2B, everything immediate. */
void emul_si4743_reset(void);

void emul_si4743_set_part(uint8_t pn);
/** A station that a seek finds and RSQ reports. */
void emul_si4743_add_station(uint8_t func, uint16_t units, uint8_t rssi, uint8_t snr, bool pilot);
/** RSSI and SNR away from any station. */
void emul_si4743_set_noise(uint8_t rssi, uint8_t snr);
/** GET_INT_STATUS reports STCINT only after this many polls (0: at once). */
void emul_si4743_stc_after(int polls);
/** The next command's CTS comes after this many status reads (-1: never). */
void emul_si4743_cts_after(int reads);
/** Every I2C transfer fails. */
void emul_si4743_fail(bool fail);
/** An RDS group into the FIFO (BLE: corrected-error levels, RESP12 layout). */
void emul_si4743_push_rds(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t ble);

bool emul_si4743_powered(void);
uint8_t emul_si4743_func(void);
uint16_t emul_si4743_units(void);
uint16_t emul_si4743_property(uint16_t id);
uint32_t emul_si4743_power_ups(void);
/** The last command written, and its length. */
const uint8_t *emul_si4743_last(uint8_t *len);

#endif /* EMUL_SI4743_H_ */
