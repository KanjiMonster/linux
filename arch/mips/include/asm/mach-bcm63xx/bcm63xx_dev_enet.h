#ifndef BCM63XX_DEV_ENET_H_
#define BCM63XX_DEV_ENET_H_

#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/platform_data/bcm63xx_enet.h>

#include <bcm63xx_regs.h>

#define ENETSW_PORTS_6328 5 /* 4 FE PHY + 1 RGMII */
#define ENETSW_PORTS_6368 6 /* 4 FE PHY + 2 RGMII */

int __init bcm63xx_enet_register(int unit,
				 const struct bcm63xx_enet_platform_data *pd);

int bcm63xx_enetsw_register(const struct bcm63xx_enetsw_platform_data *pd);


#endif /* ! BCM63XX_DEV_ENET_H_ */
