/*
 * Si4743 emulator. Section references are to AN332 Rev. 1.2.
 */

#define DT_DRV_COMPAT skyworks_si4743

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "emul_si4743.h"

#define CTS    0x80U
#define ERR    0x40U
#define STCINT 0x01U

#define MAX_STATIONS 16
#define MAX_PROPS    32
#define RDS_FIFO     25 /* groups, FMRX component 2.0 or later */

struct station {
	uint8_t func;
	uint16_t units;
	uint8_t rssi;
	uint8_t snr;
	bool pilot;
};

static struct {
	bool powered;
	uint8_t func;
	uint8_t pn;
	uint16_t units;
	bool stc;
	int stc_polls; /* GET_INT_STATUS polls left before STCINT shows */
	int stc_after;
	bool bltf;
	int cts_reads; /* status reads left before CTS (-1: never) */
	int cts_after;
	bool responding; /* false after a command the chip ignores */
	uint8_t status;
	uint8_t resp[15];
	struct station stations[MAX_STATIONS];
	int n_stations;
	uint8_t noise_rssi;
	uint8_t noise_snr;
	struct {
		uint16_t id;
		uint16_t value;
	} props[MAX_PROPS];
	int n_props;
	uint16_t rds[RDS_FIFO][4];
	uint8_t rds_ble[RDS_FIFO];
	int rds_n;
	bool failing;
	uint32_t power_ups;
	uint8_t last[8];
	uint8_t last_len;
} e;

void emul_si4743_reset(void)
{
	memset(&e, 0, sizeof(e));
	e.pn = 0x2B;
	e.noise_rssi = 5;
	e.noise_snr = 0;
	e.responding = true;
}

void emul_si4743_set_part(uint8_t pn)
{
	e.pn = pn;
}

void emul_si4743_add_station(uint8_t func, uint16_t units, uint8_t rssi, uint8_t snr, bool pilot)
{
	if (e.n_stations < MAX_STATIONS) {
		e.stations[e.n_stations++] = (struct station){func, units, rssi, snr, pilot};
	}
}

void emul_si4743_set_noise(uint8_t rssi, uint8_t snr)
{
	e.noise_rssi = rssi;
	e.noise_snr = snr;
}

void emul_si4743_stc_after(int polls)
{
	e.stc_after = polls;
}

void emul_si4743_cts_after(int reads)
{
	e.cts_after = reads;
}

void emul_si4743_fail(bool fail)
{
	e.failing = fail;
}

bool emul_si4743_powered(void)
{
	return e.powered;
}

uint8_t emul_si4743_func(void)
{
	return e.func;
}

uint16_t emul_si4743_units(void)
{
	return e.units;
}

uint32_t emul_si4743_power_ups(void)
{
	return e.power_ups;
}

const uint8_t *emul_si4743_last(uint8_t *len)
{
	*len = e.last_len;
	return e.last;
}

uint16_t emul_si4743_property(uint16_t id)
{
	for (int i = 0; i < e.n_props; i++) {
		if (e.props[i].id == id) {
			return e.props[i].value;
		}
	}
	return 0; /* the defaults these tests care about are 0 */
}

static void set_property(uint16_t id, uint16_t value)
{
	for (int i = 0; i < e.n_props; i++) {
		if (e.props[i].id == id) {
			e.props[i].value = value;
			return;
		}
	}
	if (e.n_props < MAX_PROPS) {
		e.props[e.n_props].id = id;
		e.props[e.n_props].value = value;
		e.n_props++;
	}
}

/* FM_RDS_CONFIG (0x1502): RDSEN, and block error thresholds BLETHA-D in bits 15:8. */
void emul_si4743_push_rds(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t ble)
{
	uint16_t cfg = emul_si4743_property(0x1502);

	if ((cfg & 1U) == 0U || e.rds_n >= RDS_FIFO) {
		return;
	}
	for (int blk = 0; blk < 4; blk++) {
		uint8_t level = (uint8_t)((ble >> (6 - 2 * blk)) & 3U);
		uint8_t limit = (uint8_t)((cfg >> (14 - 2 * blk)) & 3U);

		if (level > limit) {
			return; /* the chip keeps only groups within the thresholds */
		}
	}
	e.rds[e.rds_n][0] = a;
	e.rds[e.rds_n][1] = b;
	e.rds[e.rds_n][2] = c;
	e.rds[e.rds_n][3] = d;
	e.rds_ble[e.rds_n] = ble;
	e.rds_n++;
}

/* ---- The receiver ---- */

static const struct station *station_at(uint16_t units)
{
	for (int i = 0; i < e.n_stations; i++) {
		if (e.stations[i].func == e.func && e.stations[i].units == units) {
			return &e.stations[i];
		}
	}
	return NULL;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void tuned(uint16_t units)
{
	e.units = units;
	e.stc = false;
	e.stc_polls = e.stc_after;
	e.rds_n = 0; /* the FIFO is cleared by a tune or seek */
}

/* Seek with wrap over the default seek band: FM 87.5-107.9 MHz in 100 kHz,
 * AM 520-1710 kHz in 10 kHz (properties 0x1400-0x1402, 0x3400-0x3402). */
static void seek(bool up)
{
	uint16_t lo = e.func == EMUL_FUNC_FM ? 8750 : 520;
	uint16_t hi = e.func == EMUL_FUNC_FM ? 10790 : 1710;
	uint16_t step = 10;
	uint16_t start = e.units;
	uint16_t f = start;

	for (int i = 0; i <= (hi - lo) / step + 1; i++) {
		if (up) {
			f = f + step > hi ? lo : f + step;
		} else {
			f = f < lo + step ? hi : f - step;
		}
		if (f == start) {
			break;
		}
		if (station_at(f) != NULL) {
			tuned(f);
			e.bltf = false;
			return;
		}
	}
	tuned(start);
	e.bltf = true; /* wrapped back to the original frequency */
}

static bool family_ok(uint8_t cmd)
{
	uint8_t family = cmd & 0xF0U;

	return (family == 0x20U && e.func == EMUL_FUNC_FM) ||
	       (family == 0x40U && e.func == EMUL_FUNC_AM) ||
	       (family == 0x50U && e.func == EMUL_FUNC_WB);
}

static void execute(const uint8_t *c, size_t len)
{
	const struct station *st;
	uint8_t cmd = c[0];
	uint16_t f;

	memset(e.resp, 0, sizeof(e.resp));
	e.status = 0;
	e.responding = true;
	e.cts_reads = e.cts_after;

	if (!e.powered && cmd != 0x01U) {
		e.responding = false; /* "the device does not respond" (POWER_DOWN, 5.2.1) */
		return;
	}
	if ((cmd & 0xF0U) >= 0x20U && (cmd & 0xF0U) <= 0x50U && !family_ok(cmd)) {
		e.status = ERR;
		return;
	}
	switch (cmd) {
	case 0x01: /* POWER_UP: FUNC 0/1/3, XOSCEN 0 on Si474x, OPMODE analog */
		if (len != 3U || (c[1] & 0x10U) != 0U || c[2] != 0x05U ||
		    ((c[1] & 0x0FU) != EMUL_FUNC_FM && (c[1] & 0x0FU) != EMUL_FUNC_AM &&
		     (c[1] & 0x0FU) != EMUL_FUNC_WB)) {
			e.status = ERR;
			return;
		}
		e.powered = true;
		e.func = c[1] & 0x0FU;
		e.units = 0;
		e.stc = false;
		e.rds_n = 0;
		e.n_props = 0; /* properties return to their defaults */
		e.power_ups++;
		return;
	case 0x10: /* GET_REV */
		e.resp[0] = e.pn;
		e.resp[1] = '2';
		e.resp[2] = '0';
		e.resp[7] = 'C';
		return;
	case 0x11: /* POWER_DOWN */
		e.powered = false;
		return;
	case 0x12: /* SET_PROPERTY: ARG1 0, PROP, VALUE */
		if (len != 6U || c[1] != 0U) {
			e.status = ERR;
			return;
		}
		set_property((uint16_t)(c[2] << 8 | c[3]), (uint16_t)(c[4] << 8 | c[5]));
		return;
	case 0x14: /* GET_INT_STATUS */
		if (!e.stc && e.stc_polls-- <= 0) {
			e.stc = true;
		}
		e.status = e.stc ? STCINT : 0U;
		return;
	case 0x20: /* FM_TUNE_FREQ: 6400-10800 */
	case 0x40: /* AM_TUNE_FREQ: 149-23000 */
	case 0x50: /* WB_TUNE_FREQ: 64960-65020 */
		f = (uint16_t)(c[2] << 8 | c[3]);
		if ((cmd == 0x20 && (len != 5U || f < 6400 || f > 10800)) ||
		    (cmd == 0x40 && (len != 6U || f < 149 || f > 23000)) ||
		    (cmd == 0x50 && (len != 4U || f < 64960 || f > 65020))) {
			e.status = ERR;
			return;
		}
		tuned(f);
		e.bltf = false;
		return;
	case 0x21: /* FM_SEEK_START */
	case 0x41: /* AM_SEEK_START */
		if ((cmd == 0x21 && len != 2U) || (cmd == 0x41 && len != 6U) ||
		    (c[1] & 0x04U) == 0U) {
			e.status = ERR; /* this emulator only seeks with WRAP */
			return;
		}
		seek((c[1] & 0x08U) != 0U);
		return;
	case 0x22: /* FM_TUNE_STATUS */
	case 0x42: /* AM_TUNE_STATUS */
	case 0x52: /* WB_TUNE_STATUS */
		st = station_at(e.units);
		e.resp[0] = (uint8_t)((e.bltf ? 0x80U : 0U) | (st != NULL ? 0x01U : 0U));
		put16(&e.resp[1], e.units);
		e.resp[3] = st != NULL ? st->rssi : e.noise_rssi;
		e.resp[4] = st != NULL ? st->snr : e.noise_snr;
		if ((c[1] & 0x01U) != 0U) {
			e.stc = false; /* INTACK */
		}
		e.status = e.stc ? STCINT : 0U;
		return;
	case 0x23: /* FM_RSQ_STATUS */
	case 0x43: /* AM_RSQ_STATUS */
	case 0x53: /* WB_RSQ_STATUS */
		st = station_at(e.units);
		e.resp[1] = st != NULL ? 0x01U : 0U; /* VALID */
		e.resp[2] = (cmd == 0x23 && st != NULL && st->pilot) ? 0x80U | 100U : 0U;
		e.resp[3] = st != NULL ? st->rssi : e.noise_rssi;
		e.resp[4] = st != NULL ? st->snr : e.noise_snr;
		e.status = e.stc ? STCINT : 0U;
		return;
	case 0x24: /* FM_RDS_STATUS: RESP3 RDSFIFOUSED, RESP4-11 blocks A-D, RESP12 BLE */
		e.resp[2] = (uint8_t)e.rds_n;
		if (e.rds_n > 0) {
			for (int i = 0; i < 4; i++) {
				put16(&e.resp[3 + 2 * i], e.rds[0][i]);
			}
			e.resp[11] = e.rds_ble[0];
			e.rds_n--;
			memmove(e.rds[0], e.rds[1], sizeof(e.rds[0]) * (size_t)e.rds_n);
			memmove(&e.rds_ble[0], &e.rds_ble[1], (size_t)e.rds_n);
		}
		return;
	default:
		e.status = ERR;
		return;
	}
}

static int transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr)
{
	ARG_UNUSED(target);
	ARG_UNUSED(addr);
	if (e.failing || num_msgs != 1) {
		return -EIO;
	}
	if ((msgs[0].flags & I2C_MSG_READ) == 0U) {
		/* Section 6.1: at most 8 bytes, a command and seven arguments. */
		if (msgs[0].len < 1U || msgs[0].len > 8U) {
			return -EIO;
		}
		memcpy(e.last, msgs[0].buf, msgs[0].len);
		e.last_len = (uint8_t)msgs[0].len;
		execute(msgs[0].buf, msgs[0].len);
		return 0;
	}
	/* A read: the status byte, then up to 15 response bytes. */
	if (msgs[0].len < 1U || msgs[0].len > 16U) {
		return -EIO;
	}
	memset(msgs[0].buf, 0, msgs[0].len);
	if (!e.responding) {
		return 0; /* no CTS, ever */
	}
	if (e.cts_reads != 0) {
		if (e.cts_reads > 0) {
			e.cts_reads--;
		}
		return 0;
	}
	msgs[0].buf[0] = (uint8_t)(CTS | e.status);
	memcpy(&msgs[0].buf[1], e.resp, msgs[0].len - 1U);
	return 0;
}

static const struct i2c_emul_api api = {.transfer = transfer};

static int emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);
	emul_si4743_reset();
	return 0;
}

EMUL_DT_INST_DEFINE(0, emul_init, NULL, NULL, &api, NULL);

/* The I2C emulator binds to a device on the node; the HAL driver has none,
 * so the test supplies an empty one. */
DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, 90, NULL);
