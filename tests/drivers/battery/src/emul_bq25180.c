/*
 * BQ25180 emulator: single-byte register reads and writes at 0x6A
 * (SLUSE99C section 8.3.14: 7-bit address 0x6A, register address byte then
 * data). Reset values from Tables 8-9 to 8-21 where defined; STAT0 and
 * STAT1 are what the test sets.
 */

#define DT_DRV_COMPAT ti_bq25180

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "emul_bq25180.h"

static uint8_t regs[BQ25180_REGS];
static uint32_t writes;
static bool failing;

void emul_bq25180_reset(void)
{
	memset(regs, 0, sizeof(regs));
	regs[0x03] = 0x46; /* VBAT_CTRL: 4.2 V */
	regs[0x04] = 0x05; /* ICHG_CTRL: 10 mA */
	regs[0x09] = 0x11; /* SHIP_RST, Table 8-18 */
	regs[0x0C] = 0xC0; /* MASK_ID: device ID 0 = BQ25180 */
	writes = 0;
	failing = false;
}

void emul_bq25180_set(uint8_t reg, uint8_t val)
{
	regs[reg] = val;
}

uint8_t emul_bq25180_get(uint8_t reg)
{
	return regs[reg];
}

uint32_t emul_bq25180_writes(void)
{
	return writes;
}

void emul_bq25180_fail(bool fail)
{
	failing = fail;
}

static int transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr)
{
	uint8_t reg;

	ARG_UNUSED(target);
	ARG_UNUSED(addr);
	if (failing || num_msgs < 1 || (msgs[0].flags & I2C_MSG_READ) != 0U || msgs[0].len < 1U) {
		return -EIO;
	}
	reg = msgs[0].buf[0];
	if (reg >= BQ25180_REGS) {
		return -EIO;
	}
	if (num_msgs == 1) {
		/* Write: register, then data bytes to consecutive registers. */
		for (uint32_t i = 1; i < msgs[0].len && reg + i - 1U < BQ25180_REGS; i++) {
			regs[reg + i - 1U] = msgs[0].buf[i];
			writes++;
		}
		return 0;
	}
	if (num_msgs == 2 && (msgs[1].flags & I2C_MSG_READ) != 0U) {
		for (uint32_t i = 0; i < msgs[1].len; i++) {
			msgs[1].buf[i] = reg + i < BQ25180_REGS ? regs[reg + i] : 0U;
		}
		return 0;
	}
	return -EIO;
}

static const struct i2c_emul_api api = {.transfer = transfer};

static int emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);
	emul_bq25180_reset();
	return 0;
}

EMUL_DT_INST_DEFINE(0, emul_init, NULL, NULL, &api, NULL);
