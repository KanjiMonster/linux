/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2012 Jonas Gorski <jonas.gorski@gmail.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/reboot.h>
#include <asm/cacheflush.h>
#include <bcm63xx_board.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>
#include <bcm963xx_tag.h>

void bcm63xx_machine_halt(void)
{
	printk(KERN_INFO "System halted\n");
	while (1)
		;
}

static void bcm6348_a1_reboot(void)
{
	u32 reg;

	/* soft reset all blocks */
	printk(KERN_INFO "soft-resetting all blocks ...\n");
	reg = bcm_perf_readl(PERF_SOFTRESET_REG);
	reg &= ~SOFTRESET_6348_ALL;
	bcm_perf_writel(reg, PERF_SOFTRESET_REG);
	mdelay(10);

	reg = bcm_perf_readl(PERF_SOFTRESET_REG);
	reg |= SOFTRESET_6348_ALL;
	bcm_perf_writel(reg, PERF_SOFTRESET_REG);
	mdelay(10);

	/* Jump to the power on address. */
	printk(KERN_INFO "jumping to reset vector.\n");
	/* set high vectors (base at 0xbfc00000 */
	set_c0_status(ST0_BEV | ST0_ERL);
	/* run uncached in kseg0 */
	change_c0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
	__flush_cache_all();
	/* remove all wired TLB entries */
	write_c0_wired(0);
	__asm__ __volatile__(
		"jr\t%0"
		:
		: "r" (0xbfc00000));
	while (1)
		;
}

void bcm63xx_machine_reboot(void)
{
	u32 reg, perf_regs[2] = { 0, 0 };
	unsigned int i;

	/* mask and clear all external irq */
	switch (bcm63xx_get_cpu_id()) {
	case BCM6328_CPU_ID:
		perf_regs[0] = PERF_EXTIRQ_CFG_REG_6328;
		break;
	case BCM6338_CPU_ID:
		perf_regs[0] = PERF_EXTIRQ_CFG_REG_6338;
		break;
	case BCM6345_CPU_ID:
		perf_regs[0] = PERF_EXTIRQ_CFG_REG_6345;
		break;
	case BCM6348_CPU_ID:
		perf_regs[0] = PERF_EXTIRQ_CFG_REG_6348;
		break;
	case BCM6358_CPU_ID:
		perf_regs[0] = PERF_EXTIRQ_CFG_REG_6358;
		break;
	}

	for (i = 0; i < 2; i++) {
		if (!perf_regs[i])
			break;

		reg = bcm_perf_readl(perf_regs[i]);
		if (BCMCPU_IS_6348()) {
			reg &= ~EXTIRQ_CFG_MASK_ALL_6348;
			reg |= EXTIRQ_CFG_CLEAR_ALL_6348;
		} else {
			reg &= ~EXTIRQ_CFG_MASK_ALL;
			reg |= EXTIRQ_CFG_CLEAR_ALL;
		}
		bcm_perf_writel(reg, perf_regs[i]);
	}

	if (BCMCPU_IS_6348() && (bcm63xx_get_cpu_rev() == 0xa1))
		bcm6348_a1_reboot();

	printk(KERN_INFO "triggering watchdog soft-reset...\n");
	if (BCMCPU_IS_6328()) {
		bcm_wdt_writel(1, WDT_SOFTRESET_REG);
	} else {
		reg = bcm_perf_readl(PERF_SYS_PLL_CTL_REG);
		reg |= SYS_PLL_SOFT_RESET;
		bcm_perf_writel(reg, PERF_SYS_PLL_CTL_REG);
	}
	while (1)
		;
}

static void __bcm63xx_machine_reboot(char *p)
{
	bcm63xx_machine_reboot();
}

/*
 * return system type in /proc/cpuinfo
 */
const char *get_system_type(void)
{
	static char buf[128];
	snprintf(buf, sizeof(buf), "bcm63xx/%s (0x%04x/0x%04X)",
		 board_get_name(),
		 bcm63xx_get_cpu_id(), bcm63xx_get_cpu_rev());
	return buf;
}

void __init plat_time_init(void)
{
	mips_hpt_frequency = bcm63xx_get_cpu_freq() / 2;
}

void __init plat_mem_setup(void)
{
	add_memory_region(0, bcm63xx_get_memory_size(), BOOT_MEM_RAM);

	_machine_halt = bcm63xx_machine_halt;
	_machine_restart = __bcm63xx_machine_reboot;
	pm_power_off = bcm63xx_machine_halt;

	set_io_port_base(0);
	ioport_resource.start = 0;
	ioport_resource.end = ~0;

	board_setup();
}

extern struct boot_param_header __dtb_start;
extern struct boot_param_header __dtb_end;

int __init bcm63xx_is_compatible(struct boot_param_header *devtree,
				   const char *compat)
{
	unsigned long dt_root;
	struct boot_param_header *old_ibp = initial_boot_params;
	int ret;

	initial_boot_params = devtree;

	dt_root = of_get_flat_dt_root();
	ret = of_flat_dt_is_compatible(dt_root, compat);

	initial_boot_params = old_ibp;

	return ret;
}

static struct of_device_id of_ids[] = {
	{ /* will be filled at runtime */ },
	{ .compatible = "simple-bus" },
	{ },
};

static struct boot_param_header *find_compatible_tree(const char *compat)
{
	struct boot_param_header *curr = &__dtb_start;

	while (curr < &__dtb_end) {
		if (be32_to_cpu(curr->magic) != OF_DT_HEADER)
			continue;

		if (bcm63xx_is_compatible(curr, compat))
			return curr;

		/* in-kernel dtbs are aligned to 32 bytes */
		curr = (void *)curr + roundup(be32_to_cpu(curr->totalsize), 32);
	}

	return NULL;
}

void __init device_tree_init(void)
{
	struct boot_param_header *devtree = NULL;
	const char *name = board_get_name();

	strncpy(of_ids[0].compatible, name, BOARDID_LEN);

	devtree = find_compatible_tree(of_ids[0].compatible);
	if (!devtree) {
		pr_warn("no compatible device tree found for board %s, using fallback tree\n",
			of_ids[0].compatible);

		snprintf(of_ids[0].compatible, sizeof(of_ids[0].compatible),
			 "bcm9%x-generic", bcm63xx_get_cpu_id());
		devtree = find_compatible_tree(of_ids[0].compatible);

		if (!devtree)
			panic("no fallback tree available for BCM%x!\n",
			      bcm63xx_get_cpu_id());
	}

	__dt_setup_arch(devtree);
	reserve_bootmem(virt_to_phys(devtree), be32_to_cpu(devtree->totalsize),
			BOOTMEM_DEFAULT);

	unflatten_device_tree();
}

int __init bcm63xx_populate_device_tree(void)
{
	if (!of_have_populated_dt())
		panic("device tree not available\n");

	return of_platform_populate(NULL, of_ids, NULL, NULL);
}
arch_initcall(bcm63xx_populate_device_tree);

int __init bcm63xx_register_devices(void)
{
	return board_register_devices();
}

device_initcall(bcm63xx_register_devices);
