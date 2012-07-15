#ifndef BCM63XX_DEV_HSSPI_H
#define BCM63XX_DEV_HSSPI_H

#include <linux/types.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>

int __init bcm63xx_hsspi_register(void);

struct bcm63xx_hsspi_pdata {
	int		bus_num;
	u32		speed_hz;
};

#define bcm_hsspi_readl(o)	bcm_rset_readl(RSET_HSSPI, (o))
#define bcm_hsspi_writel(v, o)	bcm_rset_writel(RSET_HSSPI, (v), (o))

#define HSSPI_PLL_HZ_6328	133333333
#define HSSPI_PLL_HZ		400000000

/* TX buffer RAM is 512 bytes, but 2 bytes are reserved for the opcode. */
#define HSSPI_MAX_TX_LEN	510
#define HSSPI_MAX_RX_LEN	512
#define HSSPI_OPCODE_LEN	2

#endif /* BCM63XX_DEV_HSSPI_H */
