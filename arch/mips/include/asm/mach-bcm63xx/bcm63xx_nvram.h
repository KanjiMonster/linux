#ifndef BCM63XX_NVRAM_H
#define BCM63XX_NVRAM_H

#include <linux/if_ether.h>

#define BCM63XX_NVRAM_NAMELEN		16

/*
 * nvram structure
 */
struct bcm963xx_nvram {
	u32	version;
	u8	reserved1[256];
	u8	name[BCM63XX_NVRAM_NAMELEN];
	u32	main_tp_number;
	u32	psi_size;
	u32	mac_addr_count;
	u8	mac_addr_base[ETH_ALEN];
	u8	reserved2[2];
	u32	checksum_old;
	u8	reserved3[720];
	u32	checksum_high;
};

int __init bcm63xx_nvram_init(void *);

u8 *bcm63xx_nvram_get_name(void);

/*
 * register & return a new board mac address
 */
int bcm63xx_nvram_get_mac_address(u8 *mac);

int bcm63xx_nvram_get_psi_size(void);

#endif /* BCM63XX_NVRAM_H */
