/*
 * Bluetooth settings on nrf52_bsim: one image, one test per device.
 */

#include "bstests.h"

extern struct bst_test_list *test_radio_install(struct bst_test_list *tests);
extern struct bst_test_list *test_phone_install(struct bst_test_list *tests);

bst_test_install_t test_installers[] = {
	test_radio_install,
	test_phone_install,
	NULL,
};

int main(void)
{
	bst_main();
	return 0;
}
