/*
 * Screens. Layout is in pixels on the 144 x 168 panel; text is placed by
 * the top of its capitals ("cap top"), so lines line up whatever the font.
 *
 * Fonts: SMALL is VT323 at 17 px (cap 10), MID at 25 px (cap 14) and BIG
 * the 25 px glyphs at scale 2 (cap 28), which keeps the big digits crisp.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "app/screens.h"
#include "hal/display.h"
#include "hal/tuner.h"
#include "services/ble/ble_service.h"
#include "services/gfx/font_vt323.h"
#include "services/match/event_table.h"
#include "services/match/filter.h"

#define W        ((int)HAL_DISPLAY_WIDTH)
#define H        ((int)HAL_DISPLAY_HEIGHT)
#define MARGIN   6
#define CENTER_X (W / 2)
/* Centred text may use all but 2 pixels a side; aligned text keeps the margin. */
#define CENTER_MAX (W - 4)
#define HEADER_H 28

enum size { SMALL, MID, BIG };

static struct gfx_text_style style(enum size size, enum gfx_align align, enum gfx_color color)
{
	struct gfx_text_style st = {
		.font = size == SMALL ? &gfx_vt323_17 : &gfx_vt323_25,
		.scale = size == BIG ? 2U : 1U,
		.spacing = 0,
		.align = align,
		.color = color,
	};
	return st;
}

/* Draw s with its capitals' top at cap_top; returns the width drawn. */
static int text_at(struct gfx_canvas *c, const struct gfx_text_style *st, int x, int cap_top,
		   int max_width, const char *s)
{
	int y = cap_top - (st->font->ascent - st->font->cap_height) * st->scale;

	return gfx_text(c, st, x, y, max_width, s);
}

static void centered(struct gfx_canvas *c, enum size size, int cap_top, const char *s)
{
	struct gfx_text_style st = style(size, GFX_CENTER, GFX_BLACK);

	(void)text_at(c, &st, CENTER_X, cap_top, CENTER_MAX, s);
}

/* A label on the left and its value on the right, in SMALL. */
static void row(struct gfx_canvas *c, int cap_top, const char *label, const char *value)
{
	struct gfx_text_style left = style(SMALL, GFX_LEFT, GFX_BLACK);
	struct gfx_text_style right = style(SMALL, GFX_RIGHT, GFX_BLACK);
	int used = text_at(c, &left, MARGIN, cap_top, W / 2 - MARGIN, label);

	(void)text_at(c, &right, W - MARGIN, cap_top, W - 2 * MARGIN - used - 7, value);
}

/* ---- Formatting ---- */

static void upper(char *s)
{
	for (; *s != '\0'; s++) {
		*s = (char)toupper((unsigned char)*s);
	}
}

/* "162.550", "98.7", "1010"; "---.---" before the tuner reports. */
static void format_freq(char *buf, size_t len, uint8_t band, uint32_t khz)
{
	if (khz == 0U) {
		(void)snprintf(buf, len, "---.---");
	} else if (band == HAL_TUNER_BAND_AM) {
		(void)snprintf(buf, len, "%u", (unsigned int)khz);
	} else if (band == HAL_TUNER_BAND_FM) {
		(void)snprintf(buf, len, "%u.%u", (unsigned int)(khz / 1000U),
			       (unsigned int)(khz % 1000U / 100U));
	} else {
		(void)snprintf(buf, len, "%u.%03u", (unsigned int)(khz / 1000U),
			       (unsigned int)(khz % 1000U));
	}
}

static const char *band_name(uint8_t band)
{
	return band == HAL_TUNER_BAND_FM ? "FM" : band == HAL_TUNER_BAND_AM ? "AM" : "WX";
}

/* Battery time left: "6.2d" from 48 hours up, "14h" below. */
static void format_time_left(char *buf, size_t len, const struct ui_model *ui)
{
	unsigned int h = ui->hours_left;

	if (ui->charging) {
		(void)snprintf(buf, len, "CHG");
	} else if (h == UI_HOURS_UNKNOWN) {
		buf[0] = '\0';
	} else if (h >= 48U) {
		unsigned int tenths = (h * 10U + 12U) / 24U;

		(void)snprintf(buf, len, "%u.%ud", tenths / 10U, tenths % 10U);
	} else {
		(void)snprintf(buf, len, "%uh", h);
	}
}

static void format_hhmm(char *buf, size_t len, int64_t local_s)
{
	int64_t day_s = local_s % 86400;

	if (day_s < 0) {
		day_s += 86400;
	}
	(void)snprintf(buf, len, "%02u:%02u", (unsigned int)(day_s / 3600),
		       (unsigned int)(day_s % 3600 / 60));
}

/* ---- Parts ---- */

/* Battery outline 14 x 9 plus a 2 x 5 nub, filled in tenths. */
#define BATT_W 16

static void battery_icon(struct gfx_canvas *c, int x, int y, uint8_t percent)
{
	int fill = (percent > 100 ? 100 : percent) * 10 / 100;

	gfx_frame(c, x, y, 14, 9, 1, GFX_BLACK);
	gfx_fill(c, x + 14, y + 2, 2, 5, GFX_BLACK);
	gfx_fill(c, x + 2, y + 2, fill, 5, GFX_BLACK);
}

/* Four bars 3 pixels wide, heights 3 to 9, bottom at y; unlit bars hollow. */
static int signal_bars(struct gfx_canvas *c, int x, int bottom, uint8_t lit)
{
	for (int i = 0; i < (int)UI_SIGNAL_BARS; i++) {
		int h = 3 + 2 * i;

		if (i < lit) {
			gfx_fill(c, x + 4 * i, bottom - h, 3, h, GFX_BLACK);
		} else {
			gfx_frame(c, x + 4 * i, bottom - h, 3, h, 1, GFX_BLACK);
		}
	}
	return 4 * (int)UI_SIGNAL_BARS - 1;
}

/* Black box with white text; returns the box width. */
static int tag(struct gfx_canvas *c, int x, int cap_top, const char *s)
{
	struct gfx_text_style st = style(SMALL, GFX_LEFT, GFX_INVERT);
	int w = gfx_text_width(&st, s) + 5;

	gfx_fill(c, x, cap_top - 4, w, st.font->cap_height + 8, GFX_BLACK);
	(void)text_at(c, &st, x + 3, cap_top, 0, s);
	return w;
}

#define STATUS_CAP 6

/* Top line: band (or a WX tag), extras on the left; time left and battery on the right. */
static void status_bar(struct gfx_canvas *c, const struct ui_model *ui)
{
	struct gfx_text_style left = style(SMALL, GFX_LEFT, GFX_BLACK);
	struct gfx_text_style right = style(SMALL, GFX_RIGHT, GFX_BLACK);
	char left_text[8];
	int x = MARGIN;
	int bx = W - MARGIN - BATT_W;

	if (ui->screen == UI_SCREEN_STANDBY) {
		x += tag(c, MARGIN - 3, STATUS_CAP, "WX") + 4;
	} else {
		x += text_at(c, &left, x, STATUS_CAP, 0, band_name(ui->band)) + 6;
		if (ui->band == HAL_TUNER_BAND_FM && ui->stereo) {
			x += text_at(c, &left, x, STATUS_CAP, 0, "ST") + 6;
		}
		x += signal_bars(c, x, STATUS_CAP + left.font->cap_height, ui->signal_bars) + 6;
	}
	if (ui->phone_connected) {
		(void)text_at(c, &left, x, STATUS_CAP, 0, "BT");
	}
	battery_icon(c, bx, STATUS_CAP + 1, ui->battery_percent);
	format_time_left(left_text, sizeof(left_text), ui);
	(void)text_at(c, &right, bx - 4, STATUS_CAP, 0, left_text);
}

/*
 * Black header bar with a white title, letter-spaced up to 4 pixels while
 * it stays within HEADER_SPACED pixels (a note at the right takes 40 more),
 * and an optional SMALL note at its right.
 */
#define HEADER_SPACED 116

static void header(struct gfx_canvas *c, const char *title, const char *note)
{
	struct gfx_text_style st = style(MID, GFX_CENTER, GFX_INVERT);
	int n = (int)strlen(title);
	int room = (note != NULL ? HEADER_SPACED - 40 : HEADER_SPACED) - gfx_text_width(&st, title);

	st.spacing = (uint8_t)(n > 1 && room > 0 ? (room / (n - 1) > 4 ? 4 : room / (n - 1)) : 0);
	gfx_fill(c, 0, 0, W, HEADER_H, GFX_BLACK);
	(void)text_at(c, &st, CENTER_X, 7, note != NULL ? CENTER_MAX - 40 : CENTER_MAX, title);
	if (note != NULL) {
		struct gfx_text_style ns = style(SMALL, GFX_RIGHT, GFX_INVERT);

		(void)text_at(c, &ns, W - 4, 9, 0, note);
	}
}

/* Two SMALL lines at the foot of a screen. */
static void footer(struct gfx_canvas *c, const char *a, const char *b)
{
	if (a != NULL) {
		centered(c, SMALL, 136, a);
	}
	if (b != NULL) {
		centered(c, SMALL, 152, b);
	}
}

/*
 * Word-wrap s into at most max_lines lines of max_width pixels. Returns the
 * number of lines, or 0 if it doesn't fit (a word too long, or too many lines).
 */
#define WRAP_LINES 3

static int wrap(const struct gfx_text_style *st, const char *s, int max_width, int max_lines,
		char out[WRAP_LINES][EVENT_NAME_MAX + 1])
{
	int lines = 0;

	while (*s != '\0') {
		const char *end;
		size_t word;

		while (*s == ' ') {
			s++;
		}
		if (*s == '\0') {
			break;
		}
		end = strchr(s, ' ');
		word = end != NULL ? (size_t)(end - s) : strlen(s);
		if (lines > 0) {
			char trial[2 * (EVENT_NAME_MAX + 1)];
			size_t len = strlen(out[lines - 1]);

			if (len + 1U + word <= EVENT_NAME_MAX) {
				memcpy(trial, out[lines - 1], len);
				trial[len] = ' ';
				memcpy(&trial[len + 1U], s, word);
				trial[len + 1U + word] = '\0';
				if (gfx_text_width(st, trial) <= max_width) {
					strcpy(out[lines - 1], trial);
					s += word;
					continue;
				}
			}
		}
		if (lines == max_lines || word > EVENT_NAME_MAX ||
		    gfx_text_width_n(st, s, word) > max_width) {
			return 0;
		}
		memcpy(out[lines], s, word);
		out[lines][word] = '\0';
		lines++;
		s += word;
	}
	return lines;
}

/* ---- Screens ---- */

static void standby(struct gfx_canvas *c, const struct ui_model *ui)
{
	char freq[12];
	char value[24];
	static const char *const filters[] = {
		[FILTER_WARNINGS] = "WARNINGS",
		[FILTER_WARNINGS_WATCHES] = "WARN+WATCH",
		[FILTER_ALL] = "ALL",
		[FILTER_CUSTOM] = "CUSTOM",
	};

	status_bar(c, ui);
	format_freq(freq, sizeof(freq), HAL_TUNER_BAND_WB, ui->freq_khz);
	centered(c, BIG, 34, freq);
	centered(c, SMALL, 70, "MHz");
	centered(c, MID, 90, "MONITORING");

	if (ui->county_count == 0U) {
		(void)snprintf(value, sizeof(value), "ALL AREAS");
	} else {
		(void)snprintf(value, sizeof(value), "%u %s", (unsigned int)ui->county_count,
			       ui->county_count == 1U ? "COUNTY" : "COUNTIES");
	}
	row(c, 116, ui->travel ? "TRAVEL" : "SAME", value);
	row(c, 133, "FILTER", ui->filter < FILTER_PRESET_COUNT ? filters[ui->filter] : "?");
	if (ui->last_event[0] == '\0') {
		(void)snprintf(value, sizeof(value), "NONE");
	} else if (ui->last_local_s >= 0 && ui->local_s >= 0) {
		int64_t days = ui->local_s / 86400 - ui->last_local_s / 86400;

		if (days <= 0) {
			char hhmm[8];

			format_hhmm(hhmm, sizeof(hhmm), ui->last_local_s);
			(void)snprintf(value, sizeof(value), "%s %s", ui->last_event, hhmm);
		} else {
			(void)snprintf(value, sizeof(value), "%s %uD AGO", ui->last_event,
				       (unsigned int)(days > 99 ? 99 : days));
		}
	} else {
		(void)snprintf(value, sizeof(value), "%s", ui->last_event);
	}
	row(c, 150, "LAST", value);
}

static void listening(struct gfx_canvas *c, const struct ui_model *ui)
{
	struct gfx_text_style big = style(BIG, GFX_LEFT, GFX_BLACK);
	struct gfx_text_style small = style(SMALL, GFX_LEFT, GFX_BLACK);
	const char *unit = ui->band == HAL_TUNER_BAND_AM ? "kHz" : "MHz";
	char freq[12];
	int fw;
	int uw;

	status_bar(c, ui);
	format_freq(freq, sizeof(freq), ui->band, ui->freq_khz);
	fw = gfx_text_width(&big, freq);
	uw = gfx_text_width(&small, unit);
	if (fw + 4 + uw <= CENTER_MAX) {
		int x = CENTER_X - (fw + 4 + uw) / 2;

		(void)text_at(c, &big, x, 40, 0, freq);
		/* The unit sits on the digits' baseline. */
		(void)text_at(c, &small, x + fw + 4, 40 + 28 - small.font->cap_height, 0, unit);
	} else {
		centered(c, BIG, 34, freq);
		centered(c, SMALL, 70, unit);
	}
	if (ui->rds[0] != '\0') {
		centered(c, MID, 92, ui->rds);
	}

	/* Presets: up to four, the window holding the current one; current inverted. */
	if (ui->preset != 0U || ui->volume != UI_VOLUME_UNKNOWN) {
		int x = MARGIN - 3;
		int first = ui->preset > 4U ? ui->preset - 3 : 1;

		for (int p = first; p < first + 4 && ui->preset != 0U; p++) {
			char name[8];

			(void)snprintf(name, sizeof(name), "P%d", p);
			if (p == ui->preset) {
				x += tag(c, x, 148, name) + 2;
			} else {
				x += text_at(c, &small, x + 3, 148, 0, name) + 7;
			}
		}
		if (ui->volume != UI_VOLUME_UNKNOWN) {
			struct gfx_text_style right = style(SMALL, GFX_RIGHT, GFX_BLACK);
			char vol[8];

			(void)snprintf(vol, sizeof(vol), "VOL %u", (unsigned int)ui->volume);
			(void)text_at(c, &right, W - MARGIN, 148, 0, vol);
		}
	}
}

static void alert(struct gfx_canvas *c, const struct ui_model *ui)
{
	char name[EVENT_NAME_MAX + 1];
	char lines[WRAP_LINES][EVENT_NAME_MAX + 1];
	char note[8];
	char buf[24];
	struct gfx_text_style mid = style(MID, GFX_CENTER, GFX_BLACK);
	struct gfx_text_style small = style(SMALL, GFX_CENTER, GFX_BLACK);
	int64_t left_s = (ui->expires_ms - ui->uptime_ms) / 1000;
	int n;

	(void)snprintf(note, sizeof(note), "+%u", (unsigned int)(ui->active_alerts - 1U));
	header(c, ui->event_class == EVENT_CLASS_TEST ? "TEST" : "ALERT",
	       ui->active_alerts > 1U ? note : NULL);

	(void)snprintf(name, sizeof(name), "%s", ui->event_name);
	upper(name);
	n = wrap(&mid, name, CENTER_MAX, 2, lines);
	if (n > 0) {
		for (int i = 0; i < n; i++) {
			centered(c, MID, (n == 1 ? 50 : 38) + 22 * i, lines[i]);
		}
	} else {
		n = wrap(&small, name, CENTER_MAX, 3, lines);
		if (n == 0) {
			n = 1;
			(void)snprintf(lines[0], sizeof(lines[0]), "%s", name); /* cut by width */
		}
		for (int i = 0; i < n; i++) {
			centered(c, SMALL, 38 + 16 * i, lines[i]);
		}
	}

	if (left_s <= 0) {
		(void)snprintf(buf, sizeof(buf), "ENDING");
	} else if (ui->local_s >= 0) {
		char hhmm[8];

		format_hhmm(hhmm, sizeof(hhmm), ui->local_s + left_s);
		(void)snprintf(buf, sizeof(buf), "UNTIL %s", hhmm);
	} else if (left_s < 100 * 60) {
		(void)snprintf(buf, sizeof(buf), "FOR %u MIN", (unsigned int)((left_s + 59) / 60));
	} else {
		(void)snprintf(buf, sizeof(buf), "FOR %u HOURS", (unsigned int)((left_s + 1799) / 3600));
	}
	centered(c, MID, 90, buf);

	if (ui->matched_count == 0U) {
		(void)snprintf(buf, sizeof(buf), "ALL AREAS");
	} else {
		(void)snprintf(buf, sizeof(buf), "%u %s MATCHED", (unsigned int)ui->matched_count,
			       ui->matched_count == 1U ? "COUNTY" : "COUNTIES");
	}
	centered(c, SMALL, 112, buf);

	if (ui->alert_audio) {
		footer(c, "PLAYING BROADCAST", "ANY KEY STOPS");
	} else if (ui->alert_silenced) {
		footer(c, "SILENCED", NULL);
	} else {
		footer(c, "PLUG IN TO LISTEN", "ANY KEY SILENCES");
	}
}

/* Warnings, most urgent first: the header names the first, "+N" counts the rest. */
static const uint32_t warning_order[] = {
	UI_WARN_BATTERY_CRITICAL, UI_WARN_NO_SIGNAL,  UI_WARN_TUNER_FAULT,
	UI_WARN_NO_WEEKLY_TEST,   UI_WARN_BATTERY_LOW,
};

static void warning(struct gfx_canvas *c, const struct ui_model *ui)
{
	uint32_t first = 0;
	unsigned int others = 0;
	char note[8];
	char buf[24];

	for (size_t i = 0; i < sizeof(warning_order) / sizeof(warning_order[0]); i++) {
		if ((ui->warnings & warning_order[i]) != 0U) {
			if (first == 0U) {
				first = warning_order[i];
			} else if (!(first == UI_WARN_BATTERY_CRITICAL &&
				     warning_order[i] == UI_WARN_BATTERY_LOW)) {
				others++;
			}
		}
	}
	(void)snprintf(note, sizeof(note), "+%u", others);

	switch (first) {
	case UI_WARN_NO_SIGNAL:
		header(c, "NO SIGNAL", others > 0U ? note : NULL);
		format_freq(buf + 3, sizeof(buf) - 3, HAL_TUNER_BAND_WB, ui->freq_khz);
		memcpy(buf, "WX ", 3);
		centered(c, MID, 52, buf);
		centered(c, MID, 76, "ALERTS AT RISK");
		footer(c, "MOVE NEAR A WINDOW", "OR CHECK LANYARD");
		break;
	case UI_WARN_TUNER_FAULT:
		header(c, "TUNER FAULT", others > 0U ? note : NULL);
		centered(c, MID, 52, "RADIO CHIP");
		centered(c, MID, 76, "NOT ANSWERING");
		footer(c, "ALERTS AT RISK", "RESTARTING IT");
		break;
	case UI_WARN_NO_WEEKLY_TEST:
		header(c, "NO TEST", others > 0U ? note : NULL);
		centered(c, MID, 52, "NO WEEKLY TEST");
		centered(c, MID, 76, "IN 8 DAYS");
		footer(c, "ALERTS AT RISK", "CHECK THE CHANNEL");
		break;
	case UI_WARN_BATTERY_CRITICAL:
	case UI_WARN_BATTERY_LOW:
	default:
		header(c, "LOW BATTERY", others > 0U ? note : NULL);
		(void)snprintf(buf, sizeof(buf), "%u%%", (unsigned int)ui->battery_percent);
		centered(c, BIG, 42, buf);
		if (ui->hours_left == UI_HOURS_UNKNOWN) {
			buf[0] = '\0';
		} else if (ui->hours_left >= 48U) {
			(void)snprintf(buf, sizeof(buf), "ABOUT %u DAYS",
				       (unsigned int)((ui->hours_left + 12U) / 24U));
		} else {
			(void)snprintf(buf, sizeof(buf), "ABOUT %u HOUR%s", (unsigned int)ui->hours_left,
				       ui->hours_left == 1U ? "" : "S");
		}
		centered(c, MID, 90, buf);
		footer(c, "ALERTS STOP AT 0%", "PLUG IN USB-C");
		break;
	}
}

static void alerts_off(struct gfx_canvas *c)
{
	header(c, "ALERTS OFF", NULL);
	centered(c, MID, 52, "BATTERY EMPTY");
	centered(c, MID, 76, "RADIO IS OFF");
	footer(c, "PLUG IN USB-C", "TO TURN BACK ON");
}

static void restarted(struct gfx_canvas *c)
{
	header(c, "RESTARTED", NULL);
	centered(c, MID, 52, "RECOVERED");
	centered(c, MID, 76, "FROM A FAULT");
	footer(c, "ALERTS ARE ON", "NOTHING TO DO");
}

static void bluetooth(struct gfx_canvas *c, const struct ui_model *ui)
{
	char buf[24];

	(void)snprintf(buf, sizeof(buf), "ENDS IN %u S", (unsigned int)ui->ble_seconds);
	switch (ui->ble_screen) {
	case BLE_SCREEN_CONNECT:
		header(c, "CONNECT", NULL);
		centered(c, MID, 52, "OPEN THE APP");
		centered(c, MID, 76, "ON YOUR PHONE");
		footer(c, "PAIRED PHONES ONLY", buf);
		break;
	case BLE_SCREEN_PAIRING:
		header(c, "PAIR PHONE", NULL);
		centered(c, MID, 52, "OPEN THE APP");
		centered(c, MID, 76, "AND TAP PAIR");
		footer(c, "NEW PHONES CAN PAIR", buf);
		break;
	case BLE_SCREEN_PASSKEY: {
		char key[8];

		header(c, "PAIR PHONE", NULL);
		centered(c, MID, 46, "ENTER ON PHONE");
		(void)snprintf(key, sizeof(key), "%03u %03u", (unsigned int)(ui->passkey / 1000U % 1000U),
			       (unsigned int)(ui->passkey % 1000U));
		centered(c, BIG, 74, key);
		footer(c, NULL, buf);
		break;
	}
	case BLE_SCREEN_CONFIRM_BOND:
		header(c, "REPLACE PHONE?", NULL);
		centered(c, SMALL, 42, "2 PHONES PAIRED");
		centered(c, MID, 62, "FORGET THE");
		centered(c, MID, 84, "OLDEST PHONE?");
		footer(c, "HOLD STBY: YES", "OTHER KEY: NO");
		break;
	case BLE_SCREEN_CONFIRM_RESET:
	default:
		header(c, "FACTORY RESET?", NULL);
		centered(c, MID, 52, "ERASE SETTINGS");
		centered(c, MID, 76, "AND PHONES?");
		footer(c, "HOLD STBY: RESET", "OTHER KEY: CANCEL");
		break;
	}
}

void screens_draw(const struct ui_model *ui, struct gfx_canvas *c)
{
	gfx_clear(c, GFX_WHITE);
	switch (ui->screen) {
	case UI_SCREEN_LISTENING:
		listening(c, ui);
		break;
	case UI_SCREEN_WARNING:
		warning(c, ui);
		break;
	case UI_SCREEN_RESTARTED:
		restarted(c);
		break;
	case UI_SCREEN_ALERT:
		alert(c, ui);
		break;
	case UI_SCREEN_ALERTS_OFF:
		alerts_off(c);
		break;
	case UI_SCREEN_BLUETOOTH:
		bluetooth(c, ui);
		break;
	case UI_SCREEN_STANDBY:
	default:
		standby(c, ui);
		break;
	}
}
