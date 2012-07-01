/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Jonas Gorski <jonas.gorski@gmail.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_hsspi.h>
#include <bcm63xx_regs.h>

static struct resource spi_resources[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct bcm63xx_hsspi_pdata spi_pdata = {
	.bus_num	= 0,
};

static struct platform_device bcm63xx_hsspi_device = {
	.name		= "bcm63xx-hsspi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(spi_resources),
	.resource	= spi_resources,
	.dev		= {
		.platform_data = &spi_pdata,
	},
};

int __init bcm63xx_hsspi_register(void)
{

	if (!BCMCPU_IS_6328() && !BCMCPU_IS_6362() && !BCMCPU_IS_63268())
		return -ENODEV;

	spi_resources[0].start = bcm63xx_regset_address(RSET_HSSPI);
	spi_resources[0].end = spi_resources[0].start;
	spi_resources[0].end += RSET_HSSPI_SIZE - 1;
	spi_resources[1].start = bcm63xx_get_irq_number(IRQ_HSSPI);

	if (BCMCPU_IS_6328())
		spi_pdata.speed_hz = HSSPI_PLL_HZ_6328;
	else
		spi_pdata.speed_hz = HSSPI_PLL_HZ;

	return platform_device_register(&bcm63xx_hsspi_device);
}
