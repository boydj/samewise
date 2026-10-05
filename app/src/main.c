/*
 * Pocket WX Radio firmware entry point.
 *
 * Milestone 1 only prints a banner; the SAME decoder is exercised by the
 * test suites under tests/.
 */

#include <zephyr/kernel.h>
#include <app_version.h>

int main(void)
{
	printk("Pocket WX Radio firmware %s (%s)\n", APP_VERSION_EXTENDED_STRING,
	       CONFIG_BOARD_TARGET);
	return 0;
}
