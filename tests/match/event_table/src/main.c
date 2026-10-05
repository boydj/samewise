/*
 * Event table: building, lookup, validation and capacity.
 */

#include <zephyr/ztest.h>

#include "services/match/event_table.h"

static struct event_table t;

static void before(void *f)
{
	ARG_UNUSED(f);
	event_table_clear(&t, 7);
}

ZTEST_SUITE(event_table, NULL, NULL, before, NULL, NULL);

ZTEST(event_table, test_add_and_find)
{
	const struct event_entry *e;

	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_ok(event_table_add(&t, "SVA", "Severe Thunderstorm Watch", EVENT_CLASS_WATCH));
	zassert_ok(event_table_add(&t, "RWT", "Required Weekly Test", EVENT_CLASS_TEST));
	zassert_equal(t.version, 7);
	zassert_equal(t.count, 3);

	e = event_table_find(&t, "SVA");
	zassert_not_null(e);
	zassert_str_equal(e->name, "Severe Thunderstorm Watch");
	zassert_equal(e->cls, EVENT_CLASS_WATCH);
	zassert_equal(event_table_index(&t, "RWT"), 2, "index is insertion order");
}

ZTEST(event_table, test_lookup_needs_only_three_chars)
{
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	/* A code straight out of a header, not NUL-terminated. */
	zassert_equal(event_table_index(&t, "TOR-048453"), 0);
}

ZTEST(event_table, test_unknown_code)
{
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_is_null(event_table_find(&t, "XYZ"));
	zassert_equal(event_table_index(&t, "XYZ"), -1);
	zassert_equal(event_table_index(&t, NULL), -1);
}

ZTEST(event_table, test_rejects_bad_entries)
{
	char long_name[EVENT_NAME_MAX + 2];

	memset(long_name, 'x', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';

	zassert_equal(event_table_add(&t, "TO", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "TORR", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "tor", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "T0R", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "TOR", "", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_NAME);
	zassert_equal(event_table_add(&t, "TOR", long_name, EVENT_CLASS_WARNING),
		      EVENT_TABLE_ERR_NAME);
	zassert_equal(event_table_add(&t, "TOR", "x", EVENT_CLASS_COUNT), EVENT_TABLE_ERR_CLASS);
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_equal(event_table_add(&t, "TOR", "Again", EVENT_CLASS_WATCH),
		      EVENT_TABLE_ERR_DUPLICATE);
	zassert_equal(t.count, 1);
}

ZTEST(event_table, test_capacity)
{
	char code[4] = "AAA";

	for (int i = 0; i < EVENT_TABLE_MAX; i++) {
		code[1] = (char)('A' + i / 26);
		code[2] = (char)('A' + i % 26);
		zassert_ok(event_table_add(&t, code, "x", EVENT_CLASS_ADVISORY), "%s", code);
	}
	zassert_equal(event_table_add(&t, "ZZZ", "x", EVENT_CLASS_ADVISORY), EVENT_TABLE_ERR_FULL);
}

ZTEST(event_table, test_class_names)
{
	zassert_str_equal(event_class_name(EVENT_CLASS_WARNING), "warning");
	zassert_str_equal(event_class_name(EVENT_CLASS_TEST), "test");
	zassert_str_equal(event_class_name(99), "unknown");
}
