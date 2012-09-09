/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Jonas Gorski <jonas.gorski@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/delay.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_reset.h>

DEFINE_SPINLOCK(bcm63xx_clk_lock);

struct bcm63xx_clk {
	struct clk_hw hw;
	u32 rate;
	s8 gate_bit;
	void (*reset)(void);
};

#define to_bcm63xx_clk(p) container_of(p, struct bcm63xx_clk, hw)

static void bcm63xx_clk_set(u32 bit, int enable)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&bcm63xx_clk_lock, flags);

	val = bcm_perf_readl(PERF_CKCTL_REG);
	if (enable)
		val |= BIT(bit);
	else
		val &= ~BIT(bit);

	bcm_perf_writel(val, PERF_CKCTL_REG);

	spin_unlock_irqrestore(&bcm63xx_clk_lock, flags);

}

static int bcm63xx_clk_enable(struct clk_hw *hw)
{
	struct bcm63xx_clk *clk = to_bcm63xx_clk(hw);

	if (clk->gate_bit >= 0)
		bcm63xx_clk_set(clk->gate_bit, 1);

	if (clk->reset)
		clk->reset();

	return 0;
}

static void bcm63xx_clk_disable(struct clk_hw *hw)
{
	struct bcm63xx_clk *clk = to_bcm63xx_clk(hw);

	if (clk->gate_bit >= 0)
		bcm63xx_clk_set(clk->gate_bit, 0);
}

static int bcm63xx_clk_is_enabled(struct clk_hw *hw)
{
	struct bcm63xx_clk *clk = to_bcm63xx_clk(hw);

	if (clk->gate_bit >= 0)
		return bcm_perf_readl(PERF_CKCTL_REG) & BIT(clk->gate_bit);

	return 1;
}

static unsigned long bcm63xx_clk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_state)
{
	return to_bcm63xx_clk(hw)->rate;
}

static const struct clk_ops bcm63xx_clk_ops = {
	.enable		= bcm63xx_clk_enable,
	.disable	= bcm63xx_clk_disable,
	.is_enabled	= bcm63xx_clk_is_enabled,
	.recalc_rate	= bcm63xx_clk_recalc_rate,
};

static void bcm63xx_enetsw_reset(void)
{
	bcm63xx_core_set_reset(BCM63XX_RESET_ENETSW, 1);
	mdelay(100);
	bcm63xx_core_set_reset(BCM63XX_RESET_ENETSW, 0);
	mdelay(100);
}

static void bcm6368_enetsw_reset(void)
{
	struct clk *enetsw_sar = clk_get(NULL, "enetsw-sar");
	struct clk *enetsw_usb = clk_get(NULL, "enetsw-usb");

	/* secondary clocks need to be enabled while resetting the core */
	clk_prepare_enable(enetsw_sar);
	clk_prepare_enable(enetsw_usb);

	bcm63xx_enetsw_reset();

	clk_disable_unprepare(enetsw_usb);
	clk_disable_unprepare(enetsw_sar);

	clk_put(enetsw_sar);
	clk_put(enetsw_usb);
}

static void bcm63xx_sar_reset(void)
{
	bcm63xx_core_set_reset(BCM63XX_RESET_SAR, 1);
	mdelay(1);
	bcm63xx_core_set_reset(BCM63XX_RESET_SAR, 0);
	mdelay(1);
}

static void __init bcm63xx_clock_init(struct device_node *node,
				      void (*reset)(void))
{
	u32 gate_bit_dt, rate = 0;
	s8 gate_bit = -1;
	struct clk *clk;
	struct bcm63xx_clk *bcm63xx_clk;
	const char *clk_name = node->name;
	const char *parent = NULL;
	int num_names, i;
	struct clk_init_data init;

	if (!of_property_read_u32(node, "brcm,gate-bit", &gate_bit_dt) &&
	    !WARN_ON(gate_bit_dt > 32))
		gate_bit = gate_bit_dt;

	of_property_read_u32(node, "clock-frequency", &rate);

	num_names = of_property_count_strings(node, "clock-output-names");

	if (!WARN_ON(num_names == 0))
		of_property_read_string_index(node, "clock-output-names", 0,
					      &clk_name);

	parent = of_clk_get_parent_name(node, 0);

	bcm63xx_clk = kzalloc(sizeof(*bcm63xx_clk), GFP_KERNEL);
	if (!bcm63xx_clk)
		return;

	bcm63xx_clk->rate = rate;
	bcm63xx_clk->gate_bit = gate_bit;
	bcm63xx_clk->reset = reset;

	init.name = clk_name;
	init.ops = &bcm63xx_clk_ops;

	if (parent) {
		init.flags = 0;
		init.num_parents = 1;
		init.parent_names = &parent;
	} else {
		init.flags = CLK_IS_ROOT;
		init.num_parents = 0;
		init.parent_names = NULL;
	}

	bcm63xx_clk->hw.init = &init;

	clk = clk_register(NULL, &bcm63xx_clk->hw);
	if (IS_ERR(clk)) {
		kfree(bcm63xx_clk);
		return;
	}

	of_clk_add_provider(node, of_clk_src_simple_get, clk);
	clk_register_clkdev(clk, clk_name, NULL);

	/* register aliases */
	for (i = 1; i < num_names; i++) {
		of_property_read_string_index(node, "clock-output-names", i,
					      &clk_name);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}

static void __init bcm63xx_generic_clock_init(struct device_node *node)
{
	bcm63xx_clock_init(node, NULL);
}

static void __init bcm63xx_enetsw_clock_init(struct device_node *node)
{
	bcm63xx_clock_init(node, bcm63xx_enetsw_reset);
}

static void __init bcm6368_enetsw_clock_init(struct device_node *node)
{
	bcm63xx_clock_init(node, bcm6368_enetsw_reset);
}

static void __init bcm63xx_sar_clock_init(struct device_node *node)
{
	bcm63xx_clock_init(node, bcm63xx_sar_reset);
}

static const __initconst struct of_device_id clk_match[] = {
	{
		.compatible = "brcm,bcm63xx-clock",
		.data = bcm63xx_generic_clock_init,
	},
	{
		.compatible = "brcm,bcm63xx-enetsw-clock",
		.data = bcm63xx_enetsw_clock_init,
	},
	{
		.compatible = "brcm,bcm6368-enetsw-clock",
		.data = bcm63xx_enetsw_clock_init,
	},
	{
		.compatible = "brcm,bcm63xx-sar-clock",
		.data = bcm63xx_sar_clock_init,
	},
};

int __init bcm63xx_clocks_init(void)
{
	of_clk_init(clk_match);

	return 0;
}
arch_initcall(bcm63xx_clocks_init);
