/*
 * Settings model: defaults, validation, persistence through storage, and
 * recovery from unreadable records.
 */

#include <zephyr/ztest.h>

#include "app/settings.h"
#include "fakes/storage_fake.h"
#include "hal/storage.h"

static struct settings s, s2;

static const struct same_location travis = {0, 48, 453};
static const struct same_location bexar_nw = {1, 48, 29};

static void before(void *f)
{
	ARG_UNUSED(f);
	storage_fake_init();
	zassert_equal(settings_load(&s), 0);
}

ZTEST_SUITE(settings, NULL, NULL, before, NULL, NULL);

ZTEST(settings, test_defaults_on_blank_storage)
{
	zassert_equal(s.home.count, 0);
	zassert_equal(s.travel.count, 0);
	zassert_equal(s.mode, SETTINGS_MODE_HOME);
	zassert_equal(s.channel, SETTINGS_CHANNEL_AUTO);
	zassert_equal(s.filter, FILTER_WARNINGS_WATCHES, "spec default: warnings and watches");
	zassert_equal(storage_fake_records(), 0, "loading writes nothing");
}

ZTEST(settings, test_round_trip_through_storage)
{
	struct same_location home[2] = {travis, bexar_nw};
	uint8_t custom[SETTINGS_FILTER_BYTES] = {0x05, [15] = 0x80};
	struct event_table t;

	event_table_clear(&t, 3);
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_ok(event_table_add(&t, "RWT", "Required Weekly Test", EVENT_CLASS_TEST));

	zassert_ok(settings_set_counties(&s, SETTINGS_MODE_HOME, home, 2));
	zassert_ok(settings_set_counties(&s, SETTINGS_MODE_TRAVEL, &travis, 1));
	zassert_ok(settings_set_mode(&s, SETTINGS_MODE_TRAVEL));
	zassert_ok(settings_set_channel(&s, 7));
	zassert_ok(settings_set_filter(&s, FILTER_CUSTOM, custom));
	zassert_ok(settings_set_event_table(&s, &t));

	/* A fresh load (as after a reboot) sees everything. */
	zassert_equal(settings_load(&s2), 0);
	zassert_equal(s2.home.count, 2);
	zassert_mem_equal(&s2.home.codes[1], &bexar_nw, sizeof(bexar_nw));
	zassert_equal(s2.travel.count, 1);
	zassert_equal(s2.mode, SETTINGS_MODE_TRAVEL);
	zassert_equal(s2.channel, 7);
	zassert_equal(s2.filter, FILTER_CUSTOM);
	zassert_mem_equal(s2.custom, custom, sizeof(custom));
	zassert_equal(s2.events.version, 3);
	zassert_equal(s2.events.count, 2);
	zassert_str_equal(s2.events.entries[1].name, "Required Weekly Test");
	zassert_equal(s2.events.entries[1].cls, EVENT_CLASS_TEST);
}

ZTEST(settings, test_active_list_follows_mode)
{
	zassert_ok(settings_set_counties(&s, SETTINGS_MODE_HOME, &travis, 1));
	zassert_equal_ptr(settings_active_counties(&s), &s.home);
	zassert_ok(settings_set_mode(&s, SETTINGS_MODE_TRAVEL));
	zassert_equal_ptr(settings_active_counties(&s), &s.travel);
}

ZTEST(settings, test_rejects_invalid_values)
{
	struct same_location many[SETTINGS_MAX_COUNTIES + 1] = {0};
	struct same_location bad = {10, 48, 453};

	zassert_equal(settings_set_counties(&s, SETTINGS_MODE_HOME, many, 17), -EINVAL, "17");
	zassert_ok(settings_set_counties(&s, SETTINGS_MODE_HOME, many, 16), "16 is the limit");
	zassert_equal(settings_set_counties(&s, SETTINGS_MODE_HOME, &bad, 1), -EINVAL);
	zassert_equal(s.home.count, 16, "unchanged by a rejected write");
	zassert_equal(settings_set_mode(&s, (enum settings_mode)2), -EINVAL);
	zassert_equal(settings_set_channel(&s, 8), -EINVAL);
	zassert_equal(settings_set_filter(&s, FILTER_PRESET_COUNT, NULL), -EINVAL);
	zassert_equal(settings_set_filter(&s, FILTER_CUSTOM, NULL), -EINVAL);
}

ZTEST(settings, test_unreadable_record_falls_back_to_default)
{
	static const uint8_t wrong_schema[2] = {9, SETTINGS_MODE_TRAVEL};
	static const uint8_t bad_channel[2] = {1, 12};

	zassert_ok(hal_storage_write("mode", wrong_schema, sizeof(wrong_schema)));
	zassert_ok(hal_storage_write("channel", bad_channel, sizeof(bad_channel)));
	zassert_ok(hal_storage_write("events", "\x01\x00", 2));
	zassert_equal(settings_load(&s), 3, "three bad records reported");
	zassert_equal(s.mode, SETTINGS_MODE_HOME);
	zassert_equal(s.channel, SETTINGS_CHANNEL_AUTO);
}

ZTEST(settings, test_storage_failure_is_reported)
{
	storage_fake_fail_writes(true);
	zassert_equal(settings_set_channel(&s, 3), -EIO);
	zassert_equal(s.channel, 3, "still applied in RAM");
	storage_fake_fail_writes(false);
	zassert_equal(settings_load(&s2), 0);
	zassert_equal(s2.channel, SETTINGS_CHANNEL_AUTO, "not persisted");
}
