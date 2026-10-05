/*
 * hal/storage.h on Zephyr settings (NVS on the storage partition on the
 * board). Records live under "wx/<key>", beside the Bluetooth host's bonds.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/settings/settings.h>

#include "hal/storage.h"

#define PREFIX "wx/"

struct read_ctx {
	void *buf;
	size_t len;
	int result; /* record length, or -ENOENT */
};

static bool valid_key(const char *key)
{
	size_t n;

	if (key == NULL) {
		return false;
	}
	n = strlen(key);
	return n > 0U && n <= HAL_STORAGE_KEY_MAX;
}

static int full_key(const char *key, char *out, size_t len)
{
	int n = snprintf(out, len, PREFIX "%s", key);

	return (n > 0 && (size_t)n < len) ? 0 : -EINVAL;
}

/* Called once for the exact key (name is "" when it matches fully). */
static int read_one(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg,
		    void *param)
{
	struct read_ctx *ctx = param;
	ssize_t got;

	if (name != NULL && name[0] != '\0') {
		return 0; /* a deeper key under this one */
	}
	if (len == 0U) {
		ctx->result = -ENOENT; /* deleted */
		return 1;
	}
	got = read_cb(cb_arg, ctx->buf, len < ctx->len ? len : ctx->len);
	ctx->result = got < 0 ? (int)got : (int)len;
	return 1;
}

int hal_storage_read(const char *key, void *buf, size_t len)
{
	char name[sizeof(PREFIX) + HAL_STORAGE_KEY_MAX];
	struct read_ctx ctx = {.buf = buf, .len = len, .result = -ENOENT};
	int err;

	if (!valid_key(key) || (buf == NULL && len > 0U) || full_key(key, name, sizeof(name))) {
		return -EINVAL;
	}
	err = settings_load_subtree_direct(name, read_one, &ctx);
	return err != 0 ? err : ctx.result;
}

int hal_storage_write(const char *key, const void *data, size_t len)
{
	char name[sizeof(PREFIX) + HAL_STORAGE_KEY_MAX];

	if (!valid_key(key) || (data == NULL && len > 0U) || full_key(key, name, sizeof(name))) {
		return -EINVAL;
	}
	return settings_save_one(name, data, len);
}

int hal_storage_delete(const char *key)
{
	char name[sizeof(PREFIX) + HAL_STORAGE_KEY_MAX];

	if (!valid_key(key) || full_key(key, name, sizeof(name))) {
		return -EINVAL;
	}
	return settings_delete(name);
}

static int storage_init(void)
{
	return settings_subsys_init();
}

SYS_INIT(storage_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
