/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016 Jonas Gorski <jonas.gorski@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>

#include "pinctrl-bcm63xx.h"
#include "../core.h"

#define BANK_SIZE	sizeof(u32)
#define PINS_PER_BANK	(BANK_SIZE * BITS_PER_BYTE)

#ifdef CONFIG_OF
static int bcm63xx_gpio_of_xlate(struct gpio_chip *gc,
				 const struct of_phandle_args *gpiospec,
				 u32 *flags)
{
	struct gpio_chip *base = gpiochip_get_data(gc);
	int pin = gpiospec->args[0];

	if (gc != &base[pin / PINS_PER_BANK])
		return -EINVAL;

	pin = pin % PINS_PER_BANK;

	if (pin >= gc->ngpio)
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[1];

	return pin;
}
#endif


static int bcm63xx_setup_gpio(struct device *dev, struct gpio_chip *gc,
			      void __iomem *dirout, void __iomem *data,
			      size_t sz, int ngpio)

{
	int banks, chips, i, ret = -EINVAL;

	chips = DIV_ROUND_UP(ngpio, PINS_PER_BANK);
	banks = sz / BANK_SIZE;

	for (i = 0; i < chips; i++) {
		int offset, pins;
		int reg_offset;
		char *label;

		label = devm_kasprintf(dev, GFP_KERNEL, "bcm63xx-gpio.%i", i);
		if (!label)
			return -ENOMEM;

		offset = i * PINS_PER_BANK;
		pins = min_t(int, ngpio - offset, PINS_PER_BANK);

		/* the registers are treated like a huge big endian register */
		reg_offset = (banks - i - 1) * BANK_SIZE;

		ret = bgpio_init(&gc[i], dev, BANK_SIZE, data + reg_offset,
				 NULL, NULL, dirout + reg_offset, NULL,
				 BGPIOF_BIG_ENDIAN_BYTE_ORDER);
		if (ret)
			return ret;

		gc[i].request = gpiochip_generic_request;
		gc[i].free = gpiochip_generic_free;

#ifdef CONFIG_OF
		gc[i].of_gpio_n_cells = 2;
		gc[i].of_xlate = bcm63xx_gpio_of_xlate;
#endif

		gc[i].label = label;
		gc[i].ngpio = pins;

		devm_gpiochip_add_data(dev, &gc[i], gc);
	}

	return 0;
}

static void bcm63xx_setup_pinranges(struct gpio_chip *gc, const char *name,
				    int ngpio)
{
	int i, chips = DIV_ROUND_UP(ngpio, PINS_PER_BANK);

	for (i = 0; i < chips; i++) {
		int offset, pins;

		offset = i * PINS_PER_BANK;
		pins = min_t(int, ngpio - offset, PINS_PER_BANK);

		gpiochip_add_pin_range(&gc[i], name, 0, offset, pins);
	}
}

struct pinctrl_dev *bcm63xx_pinctrl_register(struct platform_device *pdev,
					     struct pinctrl_desc *desc,
					     void *priv, struct gpio_chip *gc,
					     int ngpio)
{
	struct pinctrl_dev *pctldev;
	struct resource *res;
	void __iomem *dirout, *data;
	size_t sz;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dirout");
	dirout = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dirout))
		return ERR_CAST(dirout);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dat");
	data = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data))
		return ERR_CAST(data);

	sz = resource_size(res);

	ret = bcm63xx_setup_gpio(&pdev->dev, gc, dirout, data, sz, ngpio);
	if (ret)
		return ERR_PTR(ret);

	pctldev = devm_pinctrl_register(&pdev->dev, desc, priv);
	if (IS_ERR(pctldev))
		return pctldev;

	bcm63xx_setup_pinranges(gc, pinctrl_dev_get_devname(pctldev), ngpio);

	dev_info(&pdev->dev, "registered at mmio %p\n", dirout);

	return pctldev;
}
