/*
 *  Broadcom BCM63XX Ath9k EEPROM fixup helper.
 *
 *  Copytight (C) 2012 Jonas Gorski <jonas.gorski@gmail.com>
 *
 *  Based on
 *
 *  Atheros AP94 reference board PCI initialization
 *
 *  Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ath9k_platform.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_nvram.h>
#include <bcm63xx_dev_pci.h>
#include <bcm63xx_dev_flash.h>
#include <bcm63xx_dev_hsspi.h>
#include <pci_ath9k_fixup.h>

struct ath9k_fixup {
	unsigned slot;
	u8 mac[ETH_ALEN];
	struct ath9k_platform_data pdata;
};

static int ath9k_num_fixups;
static struct ath9k_fixup ath9k_fixups[2] = {
	{
		.slot = 255,
		.pdata = {
			.led_pin	= -1,
		},
	},
	{
		.slot = 255,
		.pdata = {
			.led_pin	= -1,
		},
	},
};

static u16 *bcm63xx_read_eeprom(u16 *eeprom, u32 offset)
{
	u32 addr;

	if (BCMCPU_IS_6328() || BCMCPU_IS_6362() || BCMCPU_IS_63268()) {
		addr = 0x18000000;
	} else {
		addr = bcm_mpi_readl(MPI_CSBASE_REG(0));
		addr &= MPI_CSBASE_BASE_MASK;
	}

	switch (bcm63xx_attached_flash) {
	case BCM63XX_FLASH_TYPE_PARALLEL:
		memcpy(eeprom, (void *)KSEG1ADDR(addr + offset),
		       ATH9K_PLAT_EEP_MAX_WORDS * sizeof(u16));
		return eeprom;
	case BCM63XX_FLASH_TYPE_SERIAL:
		/* the first megabyte is memory mapped */
		if (offset < 0x100000) {
			memcpy(eeprom, (void *)KSEG1ADDR(addr + offset),
			       ATH9K_PLAT_EEP_MAX_WORDS * sizeof(u16));
			return eeprom;
		}

		if (BCMCPU_IS_6328() || BCMCPU_IS_6362() || BCMCPU_IS_63268()) {
			/* we can change the memory mapped megabyte */
			bcm_hsspi_writel(offset & 0xf00000, 0x18);
			memcpy(eeprom,
			       (void *)KSEG1ADDR(addr + (offset & 0xfffff)),
			       ATH9K_PLAT_EEP_MAX_WORDS * sizeof(u16));
			bcm_hsspi_writel(0, 0x18);
			return eeprom;
		}
		/* can't do anything here without using the SPI controller. */
	case BCM63XX_FLASH_TYPE_NAND:
	default:
		return NULL;
	}
}

static void ath9k_pci_fixup(struct pci_dev *dev)
{
	void __iomem *mem;
	struct ath9k_platform_data *pdata = NULL;
	u16 *cal_data = NULL;
	u16 cmd;
	u32 bar0;
	u32 val;
	unsigned i;

	for (i = 0; i < ath9k_num_fixups; i++) {
		if (ath9k_fixups[i].slot != PCI_SLOT(dev->devfn))
			continue;

		cal_data = ath9k_fixups[i].pdata.eeprom_data;
		pdata = &ath9k_fixups[i].pdata;
		break;
	}

	if (cal_data == NULL)
		return;

	if (*cal_data != 0xa55a) {
		pr_err("pci %s: invalid calibration data\n", pci_name(dev));
		return;
	}

	pr_info("pci %s: fixup device configuration\n", pci_name(dev));

	switch (bcm63xx_get_cpu_id()) {
	case BCM6328_CPU_ID:
	case BCM6362_CPU_ID:
	case BCM63168_CPU_ID:
	case BCM63268_CPU_ID:
		val = BCM_PCIE_MEM_BASE_PA;
		break;
	case BCM6348_CPU_ID:
	case BCM6358_CPU_ID:
	case BCM6368_CPU_ID:
	case BCM6369_CPU_ID:
		val = BCM_PCI_MEM_BASE_PA;
		break;
	default:
		BUG();
	}

	mem = ioremap(val, 0x10000);
	if (!mem) {
		pr_err("pci %s: ioremap error\n", pci_name(dev));
		return;
	}

	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &bar0);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &bar0);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, val);

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	/* set offset to first reg address */
	cal_data += 3;
	while (*cal_data != 0xffff) {
		u32 reg;
		reg = *cal_data++;
		val = *cal_data++;
		val |= (*cal_data++) << 16;

		writel(val, mem + reg);
		udelay(100);
	}

	pci_read_config_dword(dev, PCI_VENDOR_ID, &val);
	dev->vendor = val & 0xffff;
	dev->device = (val >> 16) & 0xffff;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &val);
	dev->revision = val & 0xff;
	dev->class = val >> 8; /* upper 3 bytes */

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd &= ~(PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, bar0);

	iounmap(mem);

	dev->dev.platform_data = pdata;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATHEROS, PCI_ANY_ID, ath9k_pci_fixup);

void __init pci_enable_ath9k_fixup(unsigned slot, u32 offset)
{
	struct ath9k_fixup *fixup;

	if (ath9k_num_fixups >= ARRAY_SIZE(ath9k_fixups))
		return;

	fixup = &ath9k_fixups[ath9k_num_fixups];

	fixup->slot = slot;

	if (!bcm63xx_read_eeprom(fixup->pdata.eeprom_data, offset))
		return;

	if (bcm63xx_nvram_get_mac_address(fixup->mac))
		return;

	fixup->pdata.macaddr = fixup->mac;
	ath9k_num_fixups++;
}
