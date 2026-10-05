/*
 * Backchannel between the simulated devices: the phones script the test
 * and ask the radio to press keys, change fakes and report its state. The
 * passkey travels this way, as a person would read it off the screen.
 */

#ifndef BSIM_BLE_LINK_H_
#define BSIM_BLE_LINK_H_

#include <stdbool.h>
#include <stdint.h>

enum {
	DEV_RADIO,
	DEV_PHONE1,
	DEV_PHONE2,
	DEV_PHONE3,
	DEV_COUNT,
};

enum link_cmd {
	/* To the radio; it answers each with LINK_STATE. */
	LINK_KEY,      /* a = enum hal_input_event_type, b = keys */
	LINK_QUERY,
	LINK_BATTERY,  /* a = percent */
	LINK_SIGNAL,   /* a = RSSI dBuV, b = SNR dB */
	LINK_DONE,     /* phone 1: the test is over */
	/* From the radio. */
	LINK_STATE,
	/* Between phones. */
	LINK_GO,
};

/* LINK_STATE.a: flags; .b: ble_screen; .c: UTC seconds; .d: passkey; .e: bonds. */
#define LINK_HFXO      0x01U
#define LINK_CONNECTED 0x02U
#define LINK_SECURE    0x04U
#define LINK_WINDOW    0x08U

struct link_msg {
	uint8_t cmd;
	uint8_t a;
	uint16_t b;
	uint32_t c;
	uint32_t d;
	uint32_t e;
};

void link_init(void);
void link_send(unsigned int dev, const struct link_msg *m);
bool link_poll(unsigned int dev, struct link_msg *m);
struct link_msg link_wait(unsigned int dev);

/* Phone helpers: a request to the radio and its state reply. */
struct link_msg radio_ask(enum link_cmd cmd, uint8_t a, uint16_t b);

#endif /* BSIM_BLE_LINK_H_ */
