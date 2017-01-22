#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/pinctrl/consumer.h>

struct mt7628_audio_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
};

static int mt7628_audio_probe(struct platform_device *pdev)
{
	struct device_node *i2s_of_node, *codec_of_node;
	struct platform_device *i2s_pdev;
	struct i2c_client *codec_dev;
	struct mt7628_audio_data *ad = NULL;
	const char *codec_dai_name;
	int ret;

	ret = of_property_read_string(pdev->dev.of_node, "codec-dai-name", &codec_dai_name);
	if (ret) {
		dev_err(&pdev->dev, "codec dai name is missing or invalid.\n");
		return ret;
	}

	i2s_of_node = of_parse_phandle(pdev->dev.of_node, "i2s-node", 0);
	codec_of_node = of_parse_phandle(pdev->dev.of_node, "codec-node", 0);
	if (!i2s_of_node || !codec_of_node) {
		dev_err(&pdev->dev, "i2s/codec handle is missing or invalid.\n");
		ret = -EINVAL;
		goto mt7628_audio_probe_error;
	}

	i2s_pdev = of_find_device_by_node(i2s_of_node);
	if (!i2s_pdev) {
		dev_err(&pdev->dev, "can not find i2s platform device.\n");
		ret = -EINVAL;
		goto mt7628_audio_probe_error;
	}
	codec_dev = of_find_i2c_device_by_node(codec_of_node);
	if (!codec_dev || !codec_dev->dev.driver) {
		dev_err(&pdev->dev, "can not find codec platform device.\n");
		ret = -EINVAL;
		goto mt7628_audio_probe_error;
	}

	ad = devm_kzalloc(&pdev->dev, sizeof(struct mt7628_audio_data), GFP_KERNEL);
	if (!ad) {
		ret = -ENOMEM;
		goto mt7628_audio_probe_error;
	}

	ad->dai.name = pdev->name;
	ad->dai.stream_name = pdev->name;
	ad->dai.codec_dai_name = codec_dai_name;
	ad->dai.codec_of_node = codec_of_node;
	ad->dai.cpu_dai_name = dev_name(&i2s_pdev->dev);
	ad->dai.platform_of_node = i2s_of_node;
	ad->dai.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			    SND_SOC_DAIFMT_CBM_CFM;

	ad->card.dev = &pdev->dev;
	ad->card.name = "mt7628-audio-card";
	ad->card.num_links = 1;
	ad->card.dai_link = &ad->dai;

	platform_set_drvdata(pdev, ad);

	ret = devm_snd_soc_register_card(&pdev->dev, &ad->card);
	if (ret) {
		dev_err(&pdev->dev, "can not register audio card.");
		goto mt7628_audio_probe_error;
	}

	of_node_put(i2s_of_node);
	of_node_put(codec_of_node);

	dev_info(&pdev->dev, "load");
	return 0;

mt7628_audio_probe_error:
	if (ad)
		devm_kfree(&pdev->dev, ad);
	if (i2s_of_node)
		of_node_put(i2s_of_node);
	if (codec_of_node)
		of_node_put(codec_of_node);

	return ret;
}

static int mt7628_audio_remove(struct platform_device *pdev)
{
	struct mt7628_audio *ad = platform_get_drvdata(pdev);
	if(ad)
		devm_kfree(&pdev->dev, ad);
	return 0;
}

static const struct of_device_id mt7628_audio_ofs[] = {
	{ .compatible = "mediatek,mt7628-audio", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt7628_audio_ofs);

static struct platform_driver mt7628_audio_driver = {
	.driver = {
		.name = "mt7628-audio",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = mt7628_audio_ofs,
	},
	.probe = mt7628_audio_probe,
	.remove = mt7628_audio_remove,
};
module_platform_driver(mt7628_audio_driver);

MODULE_AUTHOR("Qin Wei <me@vocore.io>");
MODULE_DESCRIPTION("mt7628 with audio codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mt7628-audio");
