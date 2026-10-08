/*
 * BQ25180 emulator for tests: the register file of TI SLUSE99C section
 * 8.5.1 (Table 8-7), with reset values from the register descriptions.
 */

#ifndef EMUL_BQ25180_H_
#define EMUL_BQ25180_H_

#include <stdint.h>

#define BQ25180_REGS 13

void emul_bq25180_reset(void);
void emul_bq25180_set(uint8_t reg, uint8_t val);
uint8_t emul_bq25180_get(uint8_t reg);
/** Register writes since reset. */
uint32_t emul_bq25180_writes(void);
/** Make every transfer fail with -EIO (a disconnected chip). */
void emul_bq25180_fail(bool fail);

#endif /* EMUL_BQ25180_H_ */
