/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Jonas Gorski <jonas.gorski@gmail.com>
 */

#define pr_fmt(fmt) "bcm63xx_nvram: " fmt

#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>

#include <bcm63xx_nvram.h>

#define BCM63XX_DEFAULT_PSI_SIZE	64

static struct bcm963xx_nvram nvram;
static int mac_addr_used;

int __init bcm63xx_nvram_init(void *addr)
{
	unsigned int check_len;
	u8 *p;
	u32 val;

	/* extract nvram data */
	memcpy(&nvram, addr, sizeof(nvram));

	/* check checksum before using data */
	if (nvram.version <= 4)
		check_len = offsetof(struct bcm963xx_nvram, checksum_old);
	else
		check_len = sizeof(nvram);
	val = 0;
	p = (u8 *)&nvram;

	while (check_len--)
		val += *p;
	if (val) {
		pr_err("invalid nvram checksum\n");
		return -EINVAL;
	}

	return 0;
}

u8 *bcm63xx_nvram_get_name(void)
{
	return nvram.name;
}
EXPORT_SYMBOL(bcm63xx_nvram_get_name);

int bcm63xx_nvram_get_mac_address(u8 *mac)
{
	u8 *p;
	int count;

	if (mac_addr_used >= nvram.mac_addr_count) {
		pr_err("not enough mac address\n");
		return -ENODEV;
	}

	memcpy(mac, nvram.mac_addr_base, ETH_ALEN);
	p = mac + ETH_ALEN - 1;
	count = mac_addr_used;

	while (count--) {
		do {
			(*p)++;
			if (*p != 0)
				break;
			p--;
		} while (p != mac);
	}

	if (p == mac) {
		pr_err("unable to fetch mac address\n");
		return -ENODEV;
	}

	mac_addr_used++;
	return 0;
}
EXPORT_SYMBOL(bcm63xx_nvram_get_mac_address);

int bcm63xx_nvram_get_psi_size(void)
{
	if (nvram.psi_size > 0)
		return nvram.psi_size;

	return BCM63XX_DEFAULT_PSI_SIZE;
}
EXPORT_SYMBOL(bcm63xx_nvram_get_psi_size);
