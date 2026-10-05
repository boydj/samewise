/*
 * Built-in default event table.
 *
 * To be filled from the NWS NOAA Weather Radio event code list (milestone 2
 * task 8), citing the page and its date here. Until then the default is
 * empty and every code is unknown: logged, never alerted.
 */

#include "services/match/event_table.h"

void event_table_load_default(struct event_table *t)
{
	event_table_clear(t, 0);
}
