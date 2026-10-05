/*
 * Built-in default event table, version 1.
 *
 * Sources:
 *   - NWS, "NWR NWS Event Codes", https://www.weather.gov/nwr/eventcodes
 *     (page printed Oct 4, 2026): the 53 operational NWR-SAME codes.
 *   - 47 CFR 11.31(e), Table 2 (eCFR, up to date as of Oct 1, 2026): EAN,
 *     NIC, NPT, MEP and NMN, which the NWS page doesn't list.
 *
 * Classes follow the FCC naming convention quoted on the NWS page (third
 * letter W warning, A watch, S statement), with these decisions:
 *   - TOR, SVR and EVI predate the convention: warnings.
 *   - Urgent emergencies alert as warnings: EAN, CAE, CEM, LAE.
 *   - BLU, TOE, ADR, NIC, NMN and MEP are advisories.
 *   - RWT, RMT, NPT and DMO are tests: logged, never alerted.
 * The phone can replace this table; codes it doesn't know are logged only.
 */

#include "services/match/event_table.h"

struct default_entry {
	const char *code;
	const char *name;
	enum event_class cls;
};

#define W EVENT_CLASS_WARNING
#define A EVENT_CLASS_WATCH
#define V EVENT_CLASS_ADVISORY
#define S EVENT_CLASS_STATEMENT
#define T EVENT_CLASS_TEST

static const struct default_entry defaults[] = {
	/* NWS: weather-related events */
	{"BZW", "Blizzard Warning", W},
	{"CFA", "Coastal Flood Watch", A},
	{"CFW", "Coastal Flood Warning", W},
	{"DSW", "Dust Storm Warning", W},
	{"EWW", "Extreme Wind Warning", W},
	{"FFA", "Flash Flood Watch", A},
	{"FFW", "Flash Flood Warning", W},
	{"FFS", "Flash Flood Statement", S},
	{"FLA", "Flood Watch", A},
	{"FLW", "Flood Warning", W},
	{"FLS", "Flood Statement", S},
	{"HWA", "High Wind Watch", A},
	{"HWW", "High Wind Warning", W},
	{"HUA", "Hurricane Watch", A},
	{"HUW", "Hurricane Warning", W},
	{"HLS", "Hurricane Statement", S},
	{"SVA", "Severe Thunderstorm Watch", A},
	{"SVR", "Severe Thunderstorm Warning", W},
	{"SVS", "Severe Weather Statement", S},
	{"SQW", "Snow Squall Warning", W},
	{"SMW", "Special Marine Warning", W},
	{"SPS", "Special Weather Statement", S},
	{"SSA", "Storm Surge Watch", A},
	{"SSW", "Storm Surge Warning", W},
	{"TOA", "Tornado Watch", A},
	{"TOR", "Tornado Warning", W},
	{"TRA", "Tropical Storm Watch", A},
	{"TRW", "Tropical Storm Warning", W},
	{"TSA", "Tsunami Watch", A},
	{"TSW", "Tsunami Warning", W},
	{"WSA", "Winter Storm Watch", A},
	{"WSW", "Winter Storm Warning", W},
	/* NWS: non-weather-related events */
	{"AVA", "Avalanche Watch", A},
	{"AVW", "Avalanche Warning", W},
	{"BLU", "Blue Alert", V},
	{"CAE", "Child Abduction Emergency", W},
	{"CDW", "Civil Danger Warning", W},
	{"CEM", "Civil Emergency Message", W},
	{"EQW", "Earthquake Warning", W},
	{"EVI", "Evacuation Immediate", W},
	{"FRW", "Fire Warning", W},
	{"HMW", "Hazardous Materials Warning", W},
	{"LEW", "Law Enforcement Warning", W},
	{"LAE", "Local Area Emergency", W},
	{"TOE", "911 Telephone Outage Emergency", V},
	{"NUW", "Nuclear Power Plant Warning", W},
	{"RHW", "Radiological Hazard Warning", W},
	{"SPW", "Shelter in Place Warning", W},
	{"VOW", "Volcano Warning", W},
	/* NWS: administrative events */
	{"ADR", "Administrative Message", V},
	{"DMO", "Practice/Demo Warning", T},
	{"RMT", "Required Monthly Test", T},
	{"RWT", "Required Weekly Test", T},
	/* 47 CFR 11.31 Table 2 only */
	{"EAN", "Emergency Action Notification", W},
	{"NIC", "National Information Center", V},
	{"NPT", "National Periodic Test", T},
	{"MEP", "Missing and Endangered Persons", V},
	{"NMN", "Network Message Notification", V},
};

void event_table_load_default(struct event_table *t)
{
	event_table_clear(t, 1);
	for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
		/* Checked by tests/match/event_table: every entry is valid. */
		(void)event_table_add(t, defaults[i].code, defaults[i].name, defaults[i].cls);
	}
}
