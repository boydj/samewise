/*
 * Power manager: 32 MHz crystal requests.
 */

#include "hal/clock.h"
#include "services/power/power.h"

static uint32_t clients;

void power_init(void)
{
	clients = 0; /* a reset stops the crystal */
}

void power_hfxo_request(uint32_t client)
{
	uint32_t before = clients;

	clients |= client;
	if (before == 0U && clients != 0U) {
		(void)hal_clock_hfxo_request();
	}
}

void power_hfxo_release(uint32_t client)
{
	uint32_t before = clients;

	clients &= ~client;
	if (before != 0U && clients == 0U) {
		(void)hal_clock_hfxo_release();
	}
}

bool power_hfxo_held(void)
{
	return clients != 0U;
}
