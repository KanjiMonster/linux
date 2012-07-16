/*
 * Broadcom BCM63XX High Speed SPI Controller driver
 *
 * Copyright 2000-2010 Broadcom Corporation
 * Copyright 2012 Jonas Gorski <jonas.gorski@gmail.com>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>

#include <asm/byteorder.h>

#include <bcm63xx_regs.h>
#include <bcm63xx_dev_hsspi.h>

#define HSSPI_OP_CODE_SHIFT	13
#define HSSPI_OP_SLEEP		(0 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_READ_WRITE	(1 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_WRITE		(2 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_READ		(3 << HSSPI_OP_CODE_SHIFT)

#define HSSPI_MAX_PREPEND_LEN	15

#define HSSPI_MAX_SYNC_CLOCK	30000000

struct bcm63xx_hsspi {
	struct completion	done;

	struct platform_device  *pdev;
	struct clk		*clk;
	void __iomem		*regs;
	u8 __iomem		*fifo;

	u32			speed_hz;
	int			irq;
};

static void bcm63xx_hsspi_set_clk(struct bcm63xx_hsspi *bs, int hz,
				  int profile)
{
	u32 reg;

	reg = DIV_ROUND_UP(2048, DIV_ROUND_UP(bs->speed_hz, hz));
	bcm_hsspi_writel(CLK_CTRL_ACCUM_RST_ON_LOOP | reg,
			 HSSPI_PROFILE_CLK_CTRL_REG(profile));

	reg = bcm_hsspi_readl(HSSPI_PROFILE_SIGNAL_CTRL_REG(profile));
	if (hz > HSSPI_MAX_SYNC_CLOCK)
		reg |= SIGNAL_CTRL_ASYNC_INPUT_PATH;
	else
		reg &= ~SIGNAL_CTRL_ASYNC_INPUT_PATH;
	bcm_hsspi_writel(reg, HSSPI_PROFILE_SIGNAL_CTRL_REG(profile));
}

static int bcm63xx_hsspi_do_txrx(struct spi_device *spi,
				 struct spi_transfer *first,
				 int n_transfers)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(spi->master);
	u16 pos;
	u16 opcode;
	struct spi_transfer *t;
	int i;

	for (i = 0, t = first, pos = HSSPI_OPCODE_LEN; i < n_transfers; i++) {
		if (t->tx_buf)
			memcpy_toio(&bs->fifo[pos], t->tx_buf, t->len);
		else
			memset_io(&bs->fifo[pos], 0xff, t->len);
		pos += t->len;
		t = list_entry(t->transfer_list.next,
			       struct spi_transfer, transfer_list);
	}

	opcode = cpu_to_be16(HSSPI_OP_READ_WRITE | (pos - HSSPI_OPCODE_LEN));
	memcpy_toio(bs->fifo, &opcode, sizeof(opcode));

	bcm_hsspi_writel(2 << MODE_CTRL_MULTIDATA_WR_STRT_SHIFT |
			 2 << MODE_CTRL_MULTIDATA_RD_STRT_SHIFT | 0xff,
			 HSSPI_PROFILE_MODE_CTRL_REG(spi->chip_select));

	/* enable interrupt */
	init_completion(&bs->done);
	bcm_hsspi_writel(HSSPI_PING0_CMD_DONE, HSSPI_INT_MASK_REG);

	/* start the transfer */
	bcm_hsspi_writel(spi->chip_select << PINGPONG_CMD_SS_SHIFT |
			 spi->chip_select << PINGPONG_CMD_PROFILE_SHIFT |
			 PINGPONG_COMMAND_START_NOW,
			 HSSPI_PINGPONG_COMMAND_REG(0));

	if (wait_for_completion_timeout(&bs->done, HZ) == 0) {
		dev_err(&bs->pdev->dev, "transfer timed out!\n");
		return -ETIMEDOUT;
	}

	/* no opcode slot in RX RAM */
	for (i = 0, t = first, pos = 0; i < n_transfers; i++) {
		if (t->rx_buf)
			memcpy_fromio(t->rx_buf, &bs->fifo[pos], t->len);
		pos += t->len;
		t = list_entry(t->transfer_list.next,
			       struct spi_transfer, transfer_list);
	}
	return 0;
}

static int bcm63xx_hsspi_setup(struct spi_device *spi)
{
	u32 reg;

	if (spi->bits_per_word != 8)
		return -EINVAL;

	if (spi->max_speed_hz == 0)
		return -EINVAL;

	reg = bcm_hsspi_readl(HSSPI_PROFILE_SIGNAL_CTRL_REG(spi->chip_select));
	reg &= ~(SIGNAL_CTRL_LAUNCH_RISING | SIGNAL_CTRL_LATCH_RISING);
	if (spi->mode & SPI_CPHA)
		reg |= SIGNAL_CTRL_LAUNCH_RISING;
	else
		reg |= SIGNAL_CTRL_LATCH_RISING;
	bcm_hsspi_writel(reg, HSSPI_PROFILE_SIGNAL_CTRL_REG(spi->chip_select));

	return 0;
}

static int bcm63xx_hsspi_transfer_one(struct spi_master *master,
				      struct spi_message *msg)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);
	struct spi_transfer *t, *first;
	struct spi_device *spi = msg->spi;
	u32 reg;
	int ret = -EINVAL, len, speed_hz, n_transfers;
	const int max_len = min(HSSPI_MAX_TX_LEN, HSSPI_MAX_RX_LEN);

	msg->actual_length = 0;

	first = NULL;
	len = 0;
	speed_hz = 0;
	n_transfers = 0;

	list_for_each_entry(t, &msg->transfers, transfer_list) {
		ret = -EINVAL;

		if (!t->tx_buf && !t->rx_buf)
			break;

		len += t->len;
		n_transfers++;

		if (!speed_hz && t->speed_hz <= spi->max_speed_hz)
			speed_hz = t->speed_hz;

		if (!first)
			first = t;

		/* can we add the next spi_transfer into the same HW op? */
		if (!list_is_last(&t->transfer_list, &msg->transfers)) {
			struct spi_transfer *next = list_entry(
				t->transfer_list.next, struct spi_transfer,
				transfer_list);

			if (!t->cs_change && !t->delay_usecs &&
			    next->speed_hz == t->speed_hz &&
			    len + next->len <= max_len)
				continue;
		}

		if (len > max_len)
			break;

		bcm63xx_hsspi_set_clk(bs, speed_hz ? : spi->max_speed_hz,
			spi->chip_select);

		/* setup clock polarity */
		reg = bcm_hsspi_readl(HSSPI_GLOBAL_CTRL_REG);
		reg &= ~GLOBAL_CTRL_CLK_POLARITY;
		if (spi->mode & SPI_CPOL)
			reg |= GLOBAL_CTRL_CLK_POLARITY;
		bcm_hsspi_writel(reg, HSSPI_GLOBAL_CTRL_REG);

		ret = bcm63xx_hsspi_do_txrx(msg->spi, first, n_transfers);

		if (ret == 0)
			msg->actual_length += len;
		else
			break;

		if (t->delay_usecs)
			udelay(t->delay_usecs);

		first = NULL;
		len = 0;
		speed_hz = 0;
		n_transfers = 0;
	}

	msg->status = ret;
	spi_finalize_current_message(master);
	return 0;
}

static irqreturn_t bcm63xx_hsspi_interrupt(int irq, void *dev_id)
{
	struct spi_master *master = (struct spi_master *)dev_id;
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	if (bcm_hsspi_readl(HSSPI_INT_STATUS_MASKED_REG) == 0)
		return IRQ_NONE;

	bcm_hsspi_writel(HSSPI_INT_CLEAR_ALL, HSSPI_INT_STATUS_REG);
	bcm_hsspi_writel(0, HSSPI_INT_MASK_REG);

	complete(&bs->done);

	return IRQ_HANDLED;
}

static int __devinit bcm63xx_hsspi_probe(struct platform_device *pdev)
{

	struct spi_master *master;
	struct bcm63xx_hsspi *bs;
	struct resource *res_mem;
	void __iomem *regs;
	struct device *dev = &pdev->dev;
	struct bcm63xx_hsspi_pdata *pdata = pdev->dev.platform_data;
	struct clk *clk;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq\n");
		return -ENXIO;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_request_and_ioremap(dev, res_mem);
	if (!regs) {
		dev_err(dev, "unable to ioremap regs\n");
		return -ENXIO;
	}

	clk = clk_get(dev, "hsspi");

	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto out_release;
	}

	clk_prepare_enable(clk);

	master = spi_alloc_master(&pdev->dev, sizeof(*bs));
	if (!master) {
		ret = -ENOMEM;
		goto out_disable_clk;
	}

	bs = spi_master_get_devdata(master);
	bs->pdev = pdev;
	bs->clk = clk;
	bs->regs = regs;

	master->bus_num = pdata->bus_num;
	master->num_chipselect = 8;
	master->setup = bcm63xx_hsspi_setup;
	master->transfer_one_message = bcm63xx_hsspi_transfer_one;
	master->mode_bits = SPI_CPOL | SPI_CPHA;

	bs->speed_hz = pdata->speed_hz;
	bs->fifo = (u8 __iomem *)(bs->regs + HSSPI_FIFO_REG(0));

	platform_set_drvdata(pdev, master);

	/* Initialize the hardware */
	bcm_hsspi_writel(0, HSSPI_INT_MASK_REG);

	/* clean up any pending interrupts */
	bcm_hsspi_writel(HSSPI_INT_CLEAR_ALL, HSSPI_INT_STATUS_REG);

	bcm_hsspi_writel(bcm_hsspi_readl(HSSPI_GLOBAL_CTRL_REG) |
			 GLOBAL_CTRL_CLK_GATE_SSOFF,
			 HSSPI_GLOBAL_CTRL_REG);

	ret = devm_request_irq(dev, irq, bcm63xx_hsspi_interrupt, IRQF_SHARED,
			       pdev->name, master);

	if (ret)
		goto out_put_master;

	/* register and we are done */
	ret = spi_register_master(master);
	if (ret)
		goto out_free_irq;

	return 0;

out_free_irq:
	devm_free_irq(dev, bs->irq, master);
out_put_master:
	spi_master_put(master);
out_disable_clk:
	clk_disable_unprepare(clk);
	clk_put(clk);
out_release:
	devm_ioremap_release(dev, regs);

	return ret;
}


static int __exit bcm63xx_hsspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	spi_unregister_master(master);

	/* reset the hardware and block queue progress */
	bcm_hsspi_writel(0, HSSPI_INT_MASK_REG);
	clk_disable_unprepare(bs->clk);
	clk_put(bs->clk);

	return 0;
}

#ifdef CONFIG_PM
static int bcm63xx_hsspi_suspend(struct device *dev)
{
	struct spi_master *master =
		platform_get_drvdata(to_platform_device(dev));
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	spi_master_suspend(master);
	clk_disable(bs->clk);

	return 0;
}

static int bcm63xx_hsspi_resume(struct device *dev)
{
	struct spi_master *master =
		platform_get_drvdata(to_platform_device(dev));
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	clk_enable(bs->clk);
	spi_master_resume(master);

	return 0;
}

static const struct dev_pm_ops bcm63xx_hsspi_pm_ops = {
	.suspend	= bcm63xx_hsspi_suspend,
	.resume		= bcm63xx_hsspi_resume,
};

#define BCM63XX_HSSPI_PM_OPS	(&bcm63xx_hsspi_pm_ops)
#else
#define BCM63XX_HSSPI_PM_OPS	NULL
#endif



static struct platform_driver bcm63xx_hsspi_driver = {
	.driver = {
		.name	= "bcm63xx-hsspi",
		.owner	= THIS_MODULE,
		.pm	= BCM63XX_HSSPI_PM_OPS,
	},
	.probe		= bcm63xx_hsspi_probe,
	.remove		= __exit_p(bcm63xx_hsspi_remove),
};

module_platform_driver(bcm63xx_hsspi_driver);

MODULE_ALIAS("platform:bcm63xx_hsspi");
MODULE_DESCRIPTION("Broadcom BCM63xx HS SPI Controller driver");
MODULE_AUTHOR("Jonas Gorski <jonas.gorski@gmail.com>");
MODULE_LICENSE("GPL");
