/*
 * storage fake: hal/storage.h in memory.
 *
 * Stands in for flash, so records survive clock_fake_simulate_reset() and
 * application restarts; only storage_fake_init() erases them. Writes can be
 * made to fail to test error handling.
 */

#ifndef FAKES_STORAGE_FAKE_H_
#define FAKES_STORAGE_FAKE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_FAKE_MAX_RECORDS 32
#define STORAGE_FAKE_MAX_VALUE   6144

/** Erase everything ("blank flash") and clear fault injection. */
void storage_fake_init(void);

/** Make every following write and delete fail with -EIO (or succeed again). */
void storage_fake_fail_writes(bool fail);

/** Number of successful writes since init. */
uint32_t storage_fake_writes(void);

/** Number of records held. */
uint32_t storage_fake_records(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_STORAGE_FAKE_H_ */
