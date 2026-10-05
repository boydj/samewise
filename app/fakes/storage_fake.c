/*
 * storage fake: fixed table of in-memory records.
 */

#include <errno.h>
#include <string.h>

#include "fakes/storage_fake.h"
#include "hal/storage.h"

struct record {
	bool used;
	char key[HAL_STORAGE_KEY_MAX + 1];
	uint16_t len;
	uint8_t value[STORAGE_FAKE_MAX_VALUE];
};

static struct {
	struct record rec[STORAGE_FAKE_MAX_RECORDS];
	bool fail_writes;
	uint32_t writes;
} s;

static struct record *find(const char *key)
{
	for (int i = 0; i < STORAGE_FAKE_MAX_RECORDS; i++) {
		if (s.rec[i].used && strcmp(s.rec[i].key, key) == 0) {
			return &s.rec[i];
		}
	}
	return NULL;
}

static bool valid_key(const char *key)
{
	size_t n = key == NULL ? 0U : strlen(key);

	return n > 0U && n <= HAL_STORAGE_KEY_MAX;
}

void storage_fake_init(void)
{
	memset(&s, 0, sizeof(s));
}

void storage_fake_fail_writes(bool fail)
{
	s.fail_writes = fail;
}

uint32_t storage_fake_writes(void)
{
	return s.writes;
}

uint32_t storage_fake_records(void)
{
	uint32_t n = 0;

	for (int i = 0; i < STORAGE_FAKE_MAX_RECORDS; i++) {
		n += s.rec[i].used ? 1U : 0U;
	}
	return n;
}

int hal_storage_read(const char *key, void *buf, size_t len)
{
	struct record *r;

	if (!valid_key(key) || (buf == NULL && len > 0U)) {
		return -EINVAL;
	}
	r = find(key);
	if (r == NULL) {
		return -ENOENT;
	}
	memcpy(buf, r->value, r->len < len ? r->len : len);
	return r->len;
}

int hal_storage_write(const char *key, const void *data, size_t len)
{
	struct record *r;

	if (!valid_key(key) || (data == NULL && len > 0U) || len > STORAGE_FAKE_MAX_VALUE) {
		return -EINVAL;
	}
	if (s.fail_writes) {
		return -EIO;
	}
	r = find(key);
	for (int i = 0; r == NULL && i < STORAGE_FAKE_MAX_RECORDS; i++) {
		if (!s.rec[i].used) {
			r = &s.rec[i];
			r->used = true;
			strcpy(r->key, key);
		}
	}
	if (r == NULL) {
		return -ENOSPC;
	}
	memcpy(r->value, data, len);
	r->len = (uint16_t)len;
	s.writes++;
	return 0;
}

int hal_storage_delete(const char *key)
{
	struct record *r;

	if (!valid_key(key)) {
		return -EINVAL;
	}
	if (s.fail_writes) {
		return -EIO;
	}
	r = find(key);
	if (r != NULL) {
		r->used = false;
	}
	return 0;
}
