#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

#include <ralink_regs.h>

#define I2S_REG_CFG0		0x00
#define I2S_REG_CFG0_EN		BIT(31)
#define I2S_REG_CFG0_DMA_EN	BIT(30)
#define I2S_REG_CFG0_BYTE_SWAP	BIT(28)
#define I2S_REG_CFG0_TX_EN	BIT(24)
#define I2S_REG_CFG0_RX_EN	BIT(20)
#define I2S_REG_CFG0_SLAVE	BIT(16)
#define I2S_REG_CFG0_RX_THRES	12
#define I2S_REG_CFG0_TX_THRES	4
#define I2S_REG_CFG0_DFT_THRES	(4 << I2S_REG_CFG0_RX_THRES) | \
				(4 << I2S_REG_CFG0_TX_THRES)

#define I2S_REG_INT_STATUS	0x04
#define I2S_REG_INT_EN		0x08
#define I2S_REG_FF_STATUS	0x0c
#define I2S_REG_WREG		0x10
#define I2S_REG_RREG		0x14
#define I2S_REG_CFG1		0x18

#define I2S_REG_DIVCMP		0x20
#define I2S_REG_DIVINT		0x24
#define I2S_REG_CLK_EN		BIT(31)

struct mt7628_i2s {
	struct resource *res;
	void __iomem *base;
	unsigned int sys_freq;

	struct snd_dmaengine_dai_dma_data playback_dma;
	struct snd_dmaengine_dai_dma_data capture_dma;
};

static inline uint32_t mt7628_i2s_read(const struct mt7628_i2s *i2s,
	unsigned int reg)
{
	return readl(i2s->base + reg);
}

static inline void mt7628_i2s_write(const struct mt7628_i2s *i2s,
	unsigned int reg, uint32_t value)
{
	writel(value, i2s->base + reg);
}

static int mt7628_i2s_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct mt7628_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t reg;

	if (dai->active)
		return 0;

	reg = mt7628_i2s_read(i2s, I2S_REG_CFG0);
	reg |= I2S_REG_CFG0_EN;
	mt7628_i2s_write(i2s, I2S_REG_CFG0, reg);

	return 0;
}

static void mt7628_i2s_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct mt7628_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t reg;

	if (dai->active)
		return;

	reg = mt7628_i2s_read(i2s, I2S_REG_CFG0);
	reg &= ~I2S_REG_CFG0_EN;
	mt7628_i2s_write(i2s, I2S_REG_CFG0, reg);
}

static int mt7628_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct mt7628_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	uint32_t reg;
	uint32_t mask;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mask = I2S_REG_CFG0_TX_EN;
	else
		mask = I2S_REG_CFG0_RX_EN;

	reg = mt7628_i2s_read(i2s, I2S_REG_CFG0);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		reg |= mask;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		reg &= ~mask;
		break;
	default:
		return -EINVAL;
	}

	if (reg & (I2S_REG_CFG0_TX_EN | I2S_REG_CFG0_RX_EN))
		reg |= I2S_REG_CFG0_DMA_EN;
	else
		reg &= ~I2S_REG_CFG0_DMA_EN;

	mt7628_i2s_write(i2s, I2S_REG_CFG0, reg);
	return 0;
}

static int mt7628_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mt7628_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t reg;

	reg = mt7628_i2s_read(i2s, I2S_REG_CFG0);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		reg |= I2S_REG_CFG0_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		reg &= ~I2S_REG_CFG0_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_MSB:
		reg |= I2S_REG_CFG0_BYTE_SWAP;
		break;
	case SND_SOC_DAIFMT_LSB:
		reg &= ~I2S_REG_CFG0_BYTE_SWAP;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	mt7628_i2s_write(i2s, I2S_REG_CFG0, reg);
	return 0;
}

static int mt7628_i2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct mt7628_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	/* TODO: consider to add 24bits mode, mt7628 support that hifi mode.*/
	uint32_t bitrate, divint, divcmp, freq = i2s->sys_freq / 2;
	uint64_t divsum = freq;

	/* set bitrate, two channels, left and right, each is 16bits */
	bitrate = params_rate(params) * 2 * 16;
	divint = (freq / bitrate) & 0x3ff;
	divsum = (divsum << 9) + (bitrate >> 1);
	divcmp = div_u64(divsum, bitrate) & 0x1ff;

	mt7628_i2s_write(i2s, I2S_REG_DIVINT, divint);
	mt7628_i2s_write(i2s, I2S_REG_DIVCMP, divcmp | I2S_REG_CLK_EN);

	pr_info("set audio rate to %dHz.\n", params_rate(params));
	return 0;
}

static int mt7628_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int f, int dir)
{
	uint32_t reg;

	reg = rt_sysc_r32(0x2c);
	reg &= 0xfffff1ff;
	switch (f) {
	case 12000000: reg |= 0x200; break;
	case 25000000: reg |= 0x400; break;
	case 40000000: reg |= 0x600; break;
	case 48000000: reg |= 0x800; break;
	case 0:        reg |= 0xC00; break;	/* turn off reference clock */
	default:
		pr_err("system clock supports only 12,25,40,48MHz, not %d.\n", f);
		return -EINVAL;
	}
	rt_sysc_w32(reg, 0x2c);

	pr_info("set system reference clock(mclk) to %uMHz.\n", f / 1000000);
	return 0;
}

static int mt7628_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct mt7628_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	struct snd_dmaengine_dai_dma_data *dma;
	uint32_t reg;

	dma = &i2s->playback_dma;
	dma->maxburst = 16;/* unit in words, internal 32bytes fifo. */
	dma->slave_id = 2; /* mt7628 dma i2s tx request id. */
	dma->addr = i2s->res->start + I2S_REG_WREG;
	dai->playback_dma_data = &i2s->playback_dma;

	dma = &i2s->capture_dma;
	dma->maxburst = 16;
	dma->slave_id = 3; /* mt7628 dma i2s rx request id. */
	dma->addr = i2s->res->start + I2S_REG_RREG;
	dai->capture_dma_data = &i2s->capture_dma;

	/* set share pins to reference clock mode */
	reg = rt_sysc_r32(0x60);
	reg &= 0xfffffffc;
	reg |= 0x00000002;
	rt_sysc_w32(reg, 0x60);

	mt7628_i2s_write(i2s, I2S_REG_CFG0, I2S_REG_CFG0_DFT_THRES);
	mt7628_i2s_write(i2s, I2S_REG_CFG1, 0);
	mt7628_i2s_write(i2s, I2S_REG_INT_EN, 0);

	/* default output 12MHz as codec mclk */
	mt7628_i2s_set_sysclk(dai, 0, 12000000, 0);
	return 0;
}

static int mt7628_i2s_dai_remove(struct snd_soc_dai *dai)
{
	struct mt7628_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	/* disable i2s bit clock */
	mt7628_i2s_write(i2s, I2S_REG_DIVCMP, 0);
	return 0;
}

static const struct snd_soc_dai_ops mt7628_i2s_dai_ops = {
	.startup = mt7628_i2s_startup,
	.shutdown = mt7628_i2s_shutdown,
	.trigger = mt7628_i2s_trigger,
	.hw_params = mt7628_i2s_hw_params,
	.set_fmt = mt7628_i2s_set_fmt,
	.set_sysclk = mt7628_i2s_set_sysclk,
};

static struct snd_soc_dai_driver mt7628_i2s_dai = {
	.probe = mt7628_i2s_dai_probe,
	.remove = mt7628_i2s_dai_remove,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &mt7628_i2s_dai_ops,
	.symmetric_rates = 1,
};

static const struct snd_dmaengine_pcm_config mt7628_dmaengine_pcm_config = {
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static const struct snd_soc_component_driver mt7628_i2s_component = {
	.name = "mt7628-i2s",
};

static int mt7628_i2s_dev_probe(struct platform_device *pdev)
{
	struct mt7628_i2s *i2s;
	struct clk *clk;
	int ret;

	snd_dmaengine_pcm_register(&pdev->dev, &mt7628_dmaengine_pcm_config,
		SND_DMAENGINE_PCM_FLAG_COMPAT |
		SND_DMAENGINE_PCM_FLAG_NO_RESIDUE);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get i2s system clock.\n");
		return PTR_ERR(clk);
	}

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct mt7628_i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!i2s->res) {
		devm_kfree(&pdev->dev, i2s);
		return -ENOENT;
	}

	i2s->res = request_mem_region(i2s->res->start, resource_size(i2s->res), pdev->name);
	if (!i2s->res) {
		devm_kfree(&pdev->dev, i2s);
		return -EBUSY;
	}

	i2s->base = ioremap_nocache(i2s->res->start, resource_size(i2s->res));
	if (!i2s->base) {
		ret = -EBUSY;
		goto mt7628_i2s_dev_probe_error;
	}

	platform_set_drvdata(pdev, i2s);
	ret = snd_soc_register_component(&pdev->dev, &mt7628_i2s_component, &mt7628_i2s_dai, 1);
	if (!ret) {
		i2s->sys_freq = clk_get_rate(clk);
		dev_info(&pdev->dev, "loaded, sys freq is %uMHz.\n",
			 i2s->sys_freq / 1000000);
		return 0;
	}

	dev_err(&pdev->dev, "failed to register digia audio interface.\n");
	iounmap(i2s->base);

mt7628_i2s_dev_probe_error:
	release_mem_region(i2s->res->start, resource_size(i2s->res));
	devm_kfree(&pdev->dev, i2s);
	return ret;
}

static int mt7628_i2s_dev_remove(struct platform_device *pdev)
{
	struct mt7628_i2s *i2s = platform_get_drvdata(pdev);

	snd_dmaengine_pcm_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
	iounmap(i2s->base);
	release_mem_region(i2s->res->start, resource_size(i2s->res));

	devm_kfree(&pdev->dev, i2s);
	return 0;
}

static const struct of_device_id mt7628_i2s_ofs[] = {
	{ .compatible = "mediatek,mt7628-i2s" },
	{ },
};
MODULE_DEVICE_TABLE(of, mt7628_i2s_ofs);

static struct platform_driver mt7628_i2s_driver = {
	.probe = mt7628_i2s_dev_probe,
	.remove = mt7628_i2s_dev_remove,
	.driver = {
		.name = "mt7628-i2s",
		.owner = THIS_MODULE,
		.of_match_table = mt7628_i2s_ofs,
	},
};
module_platform_driver(mt7628_i2s_driver);

MODULE_AUTHOR("Qin Wei <me@vocore.io>");
MODULE_DESCRIPTION("mt7628 i2s driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mt7628-i2s");
