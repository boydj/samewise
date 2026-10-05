/*
 * Event filter. Plain C99.
 */

#include "services/match/filter.h"

enum filter_verdict filter_decide(const struct event_table *t, const char *event,
				  enum filter_preset preset,
				  const uint8_t custom[FILTER_CUSTOM_BYTES])
{
	int i = event_table_index(t, event);
	int cls;
	bool alert;

	if (i < 0) {
		return FILTER_VERDICT_UNKNOWN;
	}
	cls = t->entries[i].cls;
	if (cls == EVENT_CLASS_TEST) {
		return FILTER_VERDICT_LOG;
	}
	switch (preset) {
	case FILTER_WARNINGS:
		alert = cls == EVENT_CLASS_WARNING;
		break;
	case FILTER_WARNINGS_WATCHES:
		alert = cls == EVENT_CLASS_WARNING || cls == EVENT_CLASS_WATCH;
		break;
	case FILTER_ALL:
		alert = true;
		break;
	case FILTER_CUSTOM:
		alert = custom != NULL && (custom[i / 8] & (1U << (i % 8))) != 0U;
		break;
	default:
		/* An unknown preset must not silence warnings: use the default. */
		alert = cls == EVENT_CLASS_WARNING || cls == EVENT_CLASS_WATCH;
		break;
	}
	return alert ? FILTER_VERDICT_ALERT : FILTER_VERDICT_LOG;
}
