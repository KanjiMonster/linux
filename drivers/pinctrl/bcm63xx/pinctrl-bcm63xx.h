#ifndef __PINCTRL_BCM63XX
#define __PINCTRL_BCM63XX

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

struct pinctrl_dev *bcm63xx_pinctrl_register(struct platform_device *pdev,
					     struct pinctrl_desc *desc,
					     void *priv, struct gpio_chip *gc,
					     int ngpio);

#endif
