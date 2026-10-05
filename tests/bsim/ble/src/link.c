/*
 * Backchannel between the simulated devices.
 */

#include <zephyr/kernel.h>

#include "argparse.h"
#include "bs_pc_backchannel.h"
#include "link.h"

/* Index in our channel list of each device (ours is unused). */
static unsigned int index_of[DEV_COUNT];

void link_init(void)
{
	unsigned int self = get_device_nbr();
	unsigned int peers[DEV_COUNT - 1];
	unsigned int channels[DEV_COUNT - 1];
	unsigned int n = 0;

	for (unsigned int d = 0; d < DEV_COUNT; d++) {
		if (d == self) {
			continue;
		}
		index_of[d] = n;
		peers[n] = d;
		channels[n] = 0;
		n++;
	}
	__ASSERT_NO_MSG(bs_open_back_channel(self, peers, channels, n) != NULL);
}

void link_send(unsigned int dev, const struct link_msg *m)
{
	bs_bc_send_msg(index_of[dev], (uint8_t *)m, sizeof(*m));
}

bool link_poll(unsigned int dev, struct link_msg *m)
{
	if (bs_bc_is_msg_received(index_of[dev]) <= 0) {
		return false;
	}
	bs_bc_receive_msg(index_of[dev], (uint8_t *)m, sizeof(*m));
	return true;
}

struct link_msg link_wait(unsigned int dev)
{
	struct link_msg m;

	while (!link_poll(dev, &m)) {
		k_sleep(K_MSEC(1));
	}
	return m;
}

struct link_msg radio_ask(enum link_cmd cmd, uint8_t a, uint16_t b)
{
	struct link_msg m = {.cmd = (uint8_t)cmd, .a = a, .b = b};

	link_send(DEV_RADIO, &m);
	m = link_wait(DEV_RADIO);
	__ASSERT(m.cmd == LINK_STATE, "radio answered %u", m.cmd);
	return m;
}
