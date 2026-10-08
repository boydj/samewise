/*
 * Every screen, drawn from a scripted model and compared pixel for pixel
 * with its golden image (golden/<name>.pbm). Build with -DGOLDEN_UPDATE=1
 * (tools/display/update_goldens.sh) to rewrite the goldens after an
 * intended change; review the new images before committing them.
 *
 * Each render is also written to the build directory (screens/<name>.pbm)
 * for tools/display/render_png.py, and a mismatch prints both images.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "app/screens.h"
#include "app/ui_model.h"
#include "fakes/host_file/host_file.h"
#include "hal/display.h"
#include "hal/tuner.h"
#include "services/ble/ble_service.h"
#include "services/match/event_table.h"
#include "services/match/filter.h"

#define STRIDE HAL_DISPLAY_STRIDE
#define FB     HAL_DISPLAY_FB_SIZE

static uint8_t fb[FB];
static struct gfx_canvas canvas = {fb, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT, STRIDE};

/* 2026-10-08 14:30 local; the clock is set unless a fixture says not. */
#define NOW_LOCAL 1791469800LL
#define MINUTE_MS 60000LL

static void base(struct ui_model *ui)
{
	ui_model_init(ui);
	ui->battery_percent = 87;
	ui->hours_left = 149;
	ui->freq_khz = 162550;
	ui->signal_bars = 3;
	ui->county_count = 3;
	ui->uptime_ms = 1000 * MINUTE_MS;
	ui->local_s = NOW_LOCAL;
}

static void alert_base(struct ui_model *ui, const char *code, const char *name, uint8_t cls)
{
	base(ui);
	ui->alert = true;
	ui->active_alerts = 1;
	memcpy(ui->event, code, 4);
	(void)snprintf(ui->event_name, sizeof(ui->event_name), "%s", name);
	ui->event_class = cls;
	ui->matched_count = 1;
	ui->expires_ms = ui->uptime_ms + 45 * MINUTE_MS;
}

/* ---- Fixtures ---- */

static void standby(struct ui_model *ui)
{
	base(ui);
}

static void standby_travel_last_charging(struct ui_model *ui)
{
	base(ui);
	ui->travel = true;
	ui->county_count = 1;
	ui->filter = FILTER_WARNINGS;
	memcpy(ui->last_event, "TOR", 4);
	ui->last_local_s = NOW_LOCAL - 25 * 60;
	ui->phone_connected = true;
	ui->charging = true;
}

static void standby_all_areas_old_alert(struct ui_model *ui)
{
	base(ui);
	ui->county_count = 0;
	ui->filter = FILTER_CUSTOM;
	memcpy(ui->last_event, "FFW", 4);
	ui->last_local_s = NOW_LOCAL - 3 * 86400;
	ui->hours_left = 30;
	ui->battery_percent = 23;
}

static void standby_untuned_clock_unset(struct ui_model *ui)
{
	base(ui);
	ui->freq_khz = 0;
	ui->signal_bars = 0;
	ui->local_s = -1;
	memcpy(ui->last_event, "SVR", 4);
	ui->hours_left = UI_HOURS_UNKNOWN;
}

static void listening_fm(struct ui_model *ui)
{
	static const char rds[] = "WXYZ-FM";

	base(ui);
	ui->listening = true;
	ui->band = HAL_TUNER_BAND_FM;
	ui->freq_khz = 98700;
	ui->stereo = true;
	memcpy(ui->rds, rds, sizeof(rds));
	ui->preset = 2;
	ui->volume = 12;
}

static void listening_am(struct ui_model *ui)
{
	base(ui);
	ui->listening = true;
	ui->band = HAL_TUNER_BAND_AM;
	ui->freq_khz = 1010;
	ui->signal_bars = 2;
}

static void listening_fm_long_rds_preset_6(struct ui_model *ui)
{
	base(ui);
	ui->listening = true;
	ui->band = HAL_TUNER_BAND_FM;
	ui->freq_khz = 101100;
	ui->signal_bars = 4;
	/* RDS radio text is up to 64 characters. */
	memset(ui->rds, 'W', UI_RDS_MAX);
	ui->rds[UI_RDS_MAX] = '\0';
	ui->preset = 6;
	ui->volume = 63;
}

static void alert_tornado(struct ui_model *ui)
{
	alert_base(ui, "TOR", "Tornado Warning", EVENT_CLASS_WARNING);
}

static void alert_silenced_two_active(struct ui_model *ui)
{
	alert_base(ui, "SVR", "Severe Thunderstorm Warning", EVENT_CLASS_WARNING);
	ui->alert_silenced = true;
	ui->active_alerts = 2;
	ui->matched_count = 2;
}

static void alert_audio_clock_unset(struct ui_model *ui)
{
	alert_base(ui, "FFW", "Flash Flood Warning", EVENT_CLASS_WARNING);
	ui->alert_audio = true;
	ui->local_s = -1;
}

static void alert_longest_name_31_counties(struct ui_model *ui)
{
	/* The longest names in the default table are 31 characters. */
	alert_base(ui, "MEP", "Missing and Endangered Persons", EVENT_CLASS_WARNING);
	ui->matched_count = 31;
	ui->local_s = -1;
	ui->expires_ms = ui->uptime_ms + 6 * 60 * MINUTE_MS;
}

static void alert_practice_demo(struct ui_model *ui)
{
	alert_base(ui, "DMO", "Practice/Demo Warning", EVENT_CLASS_TEST);
	ui->expires_ms = ui->uptime_ms + 2 * MINUTE_MS;
}

static void warning_no_signal(struct ui_model *ui)
{
	base(ui);
	ui->warnings = UI_WARN_NO_SIGNAL;
	ui->signal_bars = 0;
}

static void warning_no_signal_and_two_more(struct ui_model *ui)
{
	warning_no_signal(ui);
	ui->warnings |= UI_WARN_NO_WEEKLY_TEST | UI_WARN_BATTERY_LOW;
	ui->battery_percent = 18;
}

static void warning_no_weekly_test(struct ui_model *ui)
{
	base(ui);
	ui->warnings = UI_WARN_NO_WEEKLY_TEST;
}

static void warning_tuner_fault(struct ui_model *ui)
{
	base(ui);
	ui->warnings = UI_WARN_TUNER_FAULT;
}

static void warning_battery_low(struct ui_model *ui)
{
	base(ui);
	ui->warnings = UI_WARN_BATTERY_LOW;
	ui->battery_percent = 8;
	ui->hours_left = 10;
}

static void warning_battery_critical(struct ui_model *ui)
{
	base(ui);
	ui->warnings = UI_WARN_BATTERY_LOW | UI_WARN_BATTERY_CRITICAL;
	ui->battery_percent = 3;
	ui->hours_left = 1;
}

static void alerts_off(struct ui_model *ui)
{
	base(ui);
	ui->alerts_off = true;
	ui->battery_percent = 0;
}

static void restarted(struct ui_model *ui)
{
	base(ui);
	ui->restarted = true;
}

static void ble_connect_window(struct ui_model *ui)
{
	base(ui);
	ui->ble_screen = BLE_SCREEN_CONNECT;
	ui->ble_seconds = 118;
}

static void ble_pairing_window(struct ui_model *ui)
{
	base(ui);
	ui->ble_screen = BLE_SCREEN_PAIRING;
	ui->ble_seconds = 60;
}

static void ble_passkey(struct ui_model *ui)
{
	base(ui);
	ui->ble_screen = BLE_SCREEN_PASSKEY;
	ui->passkey = 482917;
	ui->ble_seconds = 42;
}

static void ble_passkey_leading_zeros(struct ui_model *ui)
{
	ble_passkey(ui);
	ui->passkey = 7;
}

static void ble_confirm_bond(struct ui_model *ui)
{
	base(ui);
	ui->ble_screen = BLE_SCREEN_CONFIRM_BOND;
	ui->ble_seconds = 30;
}

static void ble_confirm_reset(struct ui_model *ui)
{
	base(ui);
	ui->ble_screen = BLE_SCREEN_CONFIRM_RESET;
	ui->ble_seconds = 30;
}

struct fixture {
	const char *name;
	void (*build)(struct ui_model *ui);
	enum ui_screen screen;
};

#define FIX(fn, scr) {#fn, fn, scr}

static const struct fixture fixtures[] = {
	FIX(standby, UI_SCREEN_STANDBY),
	FIX(standby_travel_last_charging, UI_SCREEN_STANDBY),
	FIX(standby_all_areas_old_alert, UI_SCREEN_STANDBY),
	FIX(standby_untuned_clock_unset, UI_SCREEN_STANDBY),
	FIX(listening_fm, UI_SCREEN_LISTENING),
	FIX(listening_am, UI_SCREEN_LISTENING),
	FIX(listening_fm_long_rds_preset_6, UI_SCREEN_LISTENING),
	FIX(alert_tornado, UI_SCREEN_ALERT),
	FIX(alert_silenced_two_active, UI_SCREEN_ALERT),
	FIX(alert_audio_clock_unset, UI_SCREEN_ALERT),
	FIX(alert_longest_name_31_counties, UI_SCREEN_ALERT),
	FIX(alert_practice_demo, UI_SCREEN_ALERT),
	FIX(warning_no_signal, UI_SCREEN_WARNING),
	FIX(warning_no_signal_and_two_more, UI_SCREEN_WARNING),
	FIX(warning_no_weekly_test, UI_SCREEN_WARNING),
	FIX(warning_tuner_fault, UI_SCREEN_WARNING),
	FIX(warning_battery_low, UI_SCREEN_WARNING),
	FIX(warning_battery_critical, UI_SCREEN_WARNING),
	FIX(alerts_off, UI_SCREEN_ALERTS_OFF),
	FIX(restarted, UI_SCREEN_RESTARTED),
	FIX(ble_connect_window, UI_SCREEN_BLUETOOTH),
	FIX(ble_pairing_window, UI_SCREEN_BLUETOOTH),
	FIX(ble_passkey, UI_SCREEN_BLUETOOTH),
	FIX(ble_passkey_leading_zeros, UI_SCREEN_BLUETOOTH),
	FIX(ble_confirm_bond, UI_SCREEN_BLUETOOTH),
	FIX(ble_confirm_reset, UI_SCREEN_BLUETOOTH),
};

/* ---- PBM files ---- */

static const char pbm_header[] = "P4\n144 168\n";

static int write_pbm(const char *dir, const char *name, const uint8_t *img)
{
	char path[256];
	int fd;
	int err = 0;

	(void)snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
	fd = wx_host_file_open_write(path);
	if (fd < 0) {
		return -1;
	}
	if (wx_host_file_write(fd, pbm_header, sizeof(pbm_header) - 1U) < 0 ||
	    wx_host_file_write(fd, img, FB) < 0) {
		err = -1;
	}
	(void)wx_host_file_close(fd);
	return err;
}

static int read_pbm(const char *name, uint8_t *img)
{
	char path[256];
	char head[sizeof(pbm_header) - 1U];
	int fd;
	long n;

	(void)snprintf(path, sizeof(path), "%s/%s.pbm", GOLDEN_DIR, name);
	fd = wx_host_file_open_read(path);
	if (fd < 0) {
		return -1;
	}
	n = wx_host_file_read(fd, head, sizeof(head));
	if (n != (long)sizeof(head) || memcmp(head, pbm_header, sizeof(head)) != 0) {
		(void)wx_host_file_close(fd);
		return -2;
	}
	n = wx_host_file_read(fd, img, FB);
	(void)wx_host_file_close(fd);
	return n == (long)FB ? 0 : -3;
}

static bool px(const uint8_t *img, int x, int y)
{
	return (img[y * STRIDE + x / 8] & (0x80U >> (x % 8))) != 0U;
}

/* Both images side by side: '#' black, '.' white, 'X' differs. */
static void print_diff(const uint8_t *got, const uint8_t *want)
{
	for (int y = 0; y < (int)HAL_DISPLAY_HEIGHT; y++) {
		char line[HAL_DISPLAY_WIDTH + 1];

		for (int x = 0; x < (int)HAL_DISPLAY_WIDTH; x++) {
			bool g = px(got, x, y);

			line[x] = g != px(want, x, y) ? 'X' : (g ? '#' : '.');
		}
		line[HAL_DISPLAY_WIDTH] = '\0';
		printk("%3d %s\n", y, line);
	}
}

/* ---- Tests ---- */

static void render(const struct fixture *f, struct ui_model *ui)
{
	f->build(ui);
	ui_model_refresh(ui);
	zassert_equal(ui->screen, f->screen, "%s: model picks the screen", f->name);
	screens_draw(ui, &canvas);
}

ZTEST(screens, test_every_screen_matches_its_golden_image)
{
	static uint8_t golden[FB];
	struct ui_model ui;
	int failed = 0;

	for (size_t i = 0; i < ARRAY_SIZE(fixtures); i++) {
		const struct fixture *f = &fixtures[i];

		render(f, &ui);
		zassert_equal(write_pbm(RENDER_DIR, f->name, fb), 0, "%s: write render", f->name);
		if (GOLDEN_UPDATE) {
			zassert_equal(write_pbm(GOLDEN_DIR, f->name, fb), 0, "%s: write golden", f->name);
			continue;
		}
		if (read_pbm(f->name, golden) != 0) {
			printk("%s: no golden image; run tools/display/update_goldens.sh\n", f->name);
			failed++;
			continue;
		}
		if (memcmp(fb, golden, FB) != 0) {
			printk("%s differs from its golden image (X = changed pixel):\n", f->name);
			print_diff(fb, golden);
			failed++;
		}
	}
	zassert_equal(failed, 0, "%d screens differ from their golden images", failed);
}

ZTEST(screens, test_drawing_is_deterministic)
{
	static uint8_t first[FB];
	struct ui_model ui;

	for (size_t i = 0; i < ARRAY_SIZE(fixtures); i++) {
		render(&fixtures[i], &ui);
		memcpy(first, fb, FB);
		memset(fb, 0x5A, FB);
		screens_draw(&ui, &canvas);
		zassert_mem_equal(fb, first, FB, "%s: same model, same pixels", fixtures[i].name);
	}
}

/*
 * Nothing overflows: text keeps off the two outermost columns, except the
 * full-width header bar, and no screen is blank.
 */
ZTEST(screens, test_text_stays_on_screen)
{
	struct ui_model ui;

	for (size_t i = 0; i < ARRAY_SIZE(fixtures); i++) {
		const struct fixture *f = &fixtures[i];
		bool header = f->screen != UI_SCREEN_STANDBY && f->screen != UI_SCREEN_LISTENING;
		int ink = 0;

		render(f, &ui);
		for (int y = header ? 28 : 0; y < (int)HAL_DISPLAY_HEIGHT; y++) {
			for (int x = 0; x < (int)HAL_DISPLAY_WIDTH; x++) {
				if (x < 2 || x >= (int)HAL_DISPLAY_WIDTH - 2) {
					zassert_false(px(fb, x, y), "%s: ink at the edge (%d, %d)", f->name, x,
						      y);
				}
				ink += px(fb, x, y) ? 1 : 0;
			}
		}
		zassert_true(ink > 50, "%s: something is drawn", f->name);
	}
}

ZTEST_SUITE(screens, NULL, NULL, NULL, NULL, NULL);
