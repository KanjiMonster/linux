/*
 * BCM63XX CFE image tag parser
 *
 * Copyright © 2006-2008  Florian Fainelli <florian@openwrt.org>
 *			  Mike Albon <malbon@openwrt.org>
 * Copyright © 2009-2010  Daniel Dickinson <openwrt@cshore.neomailbox.net>
 * Copyright © 2011-2012  Jonas Gorski <jonas.gorski@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-bcm63xx/bcm63xx_nvram.h>
#include <asm/mach-bcm63xx/bcm963xx_tag.h>
#include <asm/mach-bcm63xx/board_bcm963xx.h>

#define BCM63XX_EXTENDED_SIZE	0xBFC00000	/* Extended flash address */

#define BCM63XX_CFE_BLOCK_SIZE	0x10000		/* always at least 64KiB */

#define BCM63XX_CFE_MAGIC_OFFSET 0x4e0

struct bcm_tag_contents {
	unsigned int		start;
	unsigned int		rootfsaddr;
	unsigned int		kerneladdr;
	unsigned int		kernellen;
	unsigned int		totallen;
	unsigned int		spareaddr;
	int			seq_no;
};

static int bcm63xx_detect_cfe(struct mtd_info *master)
{
	char buf[9];
	int ret;
	size_t retlen;

	ret = mtd_read(master, BCM963XX_CFE_VERSION_OFFSET, 5, &retlen,
		       (void *)buf);
	buf[retlen] = 0;

	if (ret)
		return ret;

	if (strncmp("cfe-v", buf, 5) == 0)
		return 0;

	/* very old CFE's do not have the cfe-v string, so check for magic */
	ret = mtd_read(master, BCM63XX_CFE_MAGIC_OFFSET, 8, &retlen,
		       (void *)buf);
	buf[retlen] = 0;

	return strncmp("CFE1CFE1", buf, 8);
}

static int bcm63xx_read_tag(struct mtd_info *master,
	struct bcm_tag_contents *tag, loff_t offset)
{
	struct bcm_tag *buf;
	size_t retlen;
	int ret;
	u32 computed_crc;

	/* Allocate memory for buffer */
	buf = vmalloc(sizeof(struct bcm_tag));
	if (!buf)
		return -ENOMEM;

	/* Get the tag */
	pr_info("Checking boot tag at offset 0x%x\n", (unsigned)offset);
	ret = mtd_read(master, offset, sizeof(struct bcm_tag), &retlen,
		       (void *)buf);

	if (retlen != sizeof(struct bcm_tag) || ret < 0) {
		vfree(buf);
		return -EIO;
	}

	computed_crc = crc32_le(IMAGETAG_CRC_START, (u8 *)buf,
				offsetof(struct bcm_tag, header_crc));
	if (computed_crc != buf->header_crc) {
		pr_warn("CFE boot tag CRC invalid (expected %08x, actual %08x)\n",
			buf->header_crc, computed_crc);
		vfree(buf);
		return -EINVAL;
	}

	sscanf(buf->flash_image_start, "%u", &tag->rootfsaddr);
	sscanf(buf->kernel_address, "%u", &tag->kerneladdr);
	sscanf(buf->kernel_length, "%u", &tag->kernellen);
	sscanf(buf->total_length, "%u", &tag->totallen);
	sscanf(buf->dual_image, "%d", &tag->seq_no);

	pr_info("Valid CFE boot tag v%s, sequence %d, board type %s\n",
		buf->tag_version, tag->seq_no, buf->board_id);

	tag->kerneladdr -= BCM63XX_EXTENDED_SIZE;
	tag->rootfsaddr -= BCM63XX_EXTENDED_SIZE;
	tag->totallen += sizeof(struct bcm_tag);

	tag->start = offset;
	tag->spareaddr = roundup(tag->totallen, master->erasesize) + tag->start;

	vfree(buf);
	return 0;
}

static int bcm63xx_parse_cfe_partitions(struct mtd_info *master,
					struct mtd_partition **pparts,
					struct mtd_part_parser_data *data)
{
	/* CFE, NVRAM and global Linux are always present */
	int nrparts = 3, curpart = 0;
	struct mtd_partition *parts;
	int ret;
	unsigned int rootfsaddr, kerneladdr, spareaddr;
	unsigned int rootfslen, kernellen, sparelen;
	unsigned int cfelen, nvramlen;
	unsigned int cfe_erasesize;
	int i, try_flip, flipped = 0;
	bool rootfs_first = false;
	struct bcm_tag_contents pri, alt, *t = NULL;
	unsigned int alttag_offset = master->size >> 1;

	if (bcm63xx_detect_cfe(master))
		return -EINVAL;

	cfe_erasesize = max_t(uint32_t, master->erasesize,
			      BCM63XX_CFE_BLOCK_SIZE);

	cfelen = cfe_erasesize;
	nvramlen = bcm63xx_nvram_get_psi_size() * 1024;
	nvramlen = roundup(nvramlen, cfe_erasesize);
	try_flip = bcm63xx_nvram_get_boot_image() == BCM63XX_BOOT_IMAGE_OLD;

	ret = bcm63xx_read_tag(master, &pri, cfelen);
	if (ret) {
		if (ret != -EINVAL) {
			pr_warn("I/O error reading primary tag, aborting\n");
			return ret;
		} else {
			if (!bcm63xx_read_tag(master, &alt, alttag_offset))
				t = &alt;
		}
	} else {
		t = &pri;
		if (t->spareaddr <= alttag_offset) {
			if (!bcm63xx_read_tag(master, &alt, alttag_offset)) {
				if ((!try_flip && alt.seq_no > pri.seq_no) ||
				    (try_flip && alt.seq_no <= pri.seq_no)) {
					t = &alt;
					flipped = try_flip;
				}
			}
		}
	}

	if (t) {
		pr_info("Using image at offset 0x%x%s\n", t->start,
			flipped ? " (flipped via nvram)" : "");
		spareaddr = t->spareaddr;
		if (t->rootfsaddr < t->kerneladdr) {
			/* default Broadcom layout */
			rootfsaddr = t->rootfsaddr;
			rootfslen = t->kerneladdr - t->rootfsaddr;
			rootfs_first = true;
		} else {
			/* OpenWrt layout */
			rootfsaddr = t->kerneladdr + t->kernellen;
			rootfslen = spareaddr - rootfsaddr;
		}
		kerneladdr = t->kerneladdr;
		kernellen = t->kernellen;
	} else {
		rootfsaddr = kerneladdr = 0;
		rootfslen = kernellen = 0;
		spareaddr = cfelen;
	}
	sparelen = master->size - spareaddr - nvramlen;

	/* Determine number of partitions */
	if (rootfslen > 0)
		nrparts++;

	if (kernellen > 0)
		nrparts++;

	/* Ask kernel for more memory */
	parts = kzalloc(sizeof(*parts) * nrparts + 10 * nrparts, GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	/* Start building partition list */
	parts[curpart].name = "CFE";
	parts[curpart].offset = 0;
	parts[curpart].size = cfelen;
	curpart++;

	if (kernellen > 0) {
		int kernelpart = curpart;

		if (rootfslen > 0 && rootfs_first)
			kernelpart++;
		parts[kernelpart].name = "kernel";
		parts[kernelpart].offset = kerneladdr;
		parts[kernelpart].size = kernellen;
		curpart++;
	}

	if (rootfslen > 0) {
		int rootfspart = curpart;

		if (kernellen > 0 && rootfs_first)
			rootfspart--;
		parts[rootfspart].name = "rootfs";
		parts[rootfspart].offset = rootfsaddr;
		parts[rootfspart].size = rootfslen;
		if (sparelen > 0 && !rootfs_first)
			parts[rootfspart].size += sparelen;
		curpart++;
	}

	parts[curpart].name = "nvram";
	parts[curpart].offset = master->size - nvramlen;
	parts[curpart].size = nvramlen;
	curpart++;

	/* Global partition "linux" to make easy firmware upgrade */
	parts[curpart].name = "linux";
	parts[curpart].offset = cfelen;
	parts[curpart].size = master->size - cfelen - nvramlen;

	for (i = 0; i < nrparts; i++)
		pr_info("Partition %d is %s offset %llx and length %llx\n", i,
			parts[i].name, parts[i].offset,	parts[i].size);

	pr_info("Spare partition is offset %x and length %x\n",	spareaddr,
		sparelen);

	*pparts = parts;

	return nrparts;
};

static struct mtd_part_parser bcm63xx_cfe_parser = {
	.owner = THIS_MODULE,
	.parse_fn = bcm63xx_parse_cfe_partitions,
	.name = "bcm63xxpart",
};

static int __init bcm63xx_cfe_parser_init(void)
{
	return register_mtd_parser(&bcm63xx_cfe_parser);
}

static void __exit bcm63xx_cfe_parser_exit(void)
{
	deregister_mtd_parser(&bcm63xx_cfe_parser);
}

module_init(bcm63xx_cfe_parser_init);
module_exit(bcm63xx_cfe_parser_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Dickinson <openwrt@cshore.neomailbox.net>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Mike Albon <malbon@openwrt.org>");
MODULE_AUTHOR("Jonas Gorski <jonas.gorski@gmail.com");
MODULE_DESCRIPTION("MTD partitioning for BCM63XX CFE bootloaders");
