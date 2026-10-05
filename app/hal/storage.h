/*
 * Storage interface: settings and the alert log as key-value records.
 *
 * Zephyr settings on flash on the board; a local file on native_sim. Writes
 * can take tens of milliseconds (flash erase), so the alert path hands
 * records to a lower-priority thread instead of calling these directly.
 */

#ifndef HAL_STORAGE_H_
#define HAL_STORAGE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest key, excluding the terminating NUL. */
#define HAL_STORAGE_KEY_MAX 32U

/**
 * Read a record.
 *
 * @return its length in bytes (it is truncated to len if longer),
 *         -ENOENT if absent, or another negative errno.
 */
int hal_storage_read(const char *key, void *buf, size_t len);

/** Create or replace a record. */
int hal_storage_write(const char *key, const void *data, size_t len);

/** Delete a record; 0 if it did not exist. */
int hal_storage_delete(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* HAL_STORAGE_H_ */
