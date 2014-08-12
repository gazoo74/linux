/*
 * LED Morse Trigger
 *
 * Copyright (C) 2013 Gaël Portay <g.portay@overkiz.com>
 *
 * Based on Richard Purdie's ledtrig-timer.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/leds.h>
#include <linux/string.h>
#include <linux/morse-encoding.h>

#include "../leds.h"

#define timer_to_morse_trig(timer) \
	container_of(timer, struct morse_trig_data, timer)

struct morse_trig_data {
	struct led_classdev *cdev;
	struct morse_encoding menc;
};

static int morse_trig_function(struct morse_trig_data *data, bool on)
{
	struct led_classdev *led_cdev = data->cdev;

	__led_set_brightness(led_cdev, on ? led_cdev->max_brightness : LED_OFF);

	return 0;
}

static ssize_t morse_message_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;

	return snprintf(buf, PAGE_SIZE, "%s\n", data->menc.message);
}

static ssize_t morse_message_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;
	ssize_t ret;

	ret = morse_encoding_set_message(&data->menc, buf);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR(message, 0666, morse_message_show, morse_message_store);

static ssize_t morse_raw_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;
	ssize_t size;

	size = morse_encoding_get_raw(&data->menc, buf, PAGE_SIZE);

	if (size < 0)
		return size;

	size += snprintf(&buf[size], PAGE_SIZE - size, "\n");

	return size;
}

static DEVICE_ATTR(raw, 0444, morse_raw_show, NULL);

static ssize_t morse_repeat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;

	return snprintf(buf, PAGE_SIZE, "%u\n", data->menc.repeat);
}

static ssize_t morse_repeat_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;
	unsigned long repeat;
	int err;

	err = kstrtoul(buf, 0, &repeat);
	if (err)
		return -EINVAL;

	morse_encoding_set_repeat(&data->menc, repeat);

	return size;
}

static DEVICE_ATTR(repeat, 0644, morse_repeat_show, morse_repeat_store);

static ssize_t morse_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;

	return snprintf(buf, PAGE_SIZE, "%d\n", data->menc.count);
}

static DEVICE_ATTR(count, 0444, morse_count_show, NULL);

static ssize_t morse_timeunit_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;
	unsigned long timeunit = div_s64(ktime_to_ns(data->menc.intra_gap),
						     1000000);

	return snprintf(buf, PAGE_SIZE, "%lu\n", timeunit);
}

static ssize_t morse_timeunit_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data *data = led_cdev->trigger_data;
	unsigned long timeunit;
	int err;

	err = kstrtoul(buf, 0, &timeunit);
	if (err)
		return -EINVAL;

	morse_encoding_set_timeunit_us(&data->menc, timeunit);

	return size;
}

static DEVICE_ATTR(interval, 0644, morse_timeunit_show, morse_timeunit_store);

static void morse_trig_activate(struct led_classdev *led_cdev)
{
	struct morse_trig_data *data;

	data = kzalloc(sizeof(struct morse_encoding), GFP_KERNEL);
	if (!data)
		return;

	led_cdev->trigger_data = data;

	data->cdev = led_cdev;

	morse_encoding_init(&data->menc, data,
			    (int (*) (void *, bool)) morse_trig_function);

	device_create_file(led_cdev->dev, &dev_attr_message);
	device_create_file(led_cdev->dev, &dev_attr_raw);
	device_create_file(led_cdev->dev, &dev_attr_repeat);
	device_create_file(led_cdev->dev, &dev_attr_count);
	device_create_file(led_cdev->dev, &dev_attr_interval);
}

static void morse_trig_deactivate(struct led_classdev *led_cdev)
{
	struct morse_trig_data *data = led_cdev->trigger_data;

	device_remove_file(led_cdev->dev, &dev_attr_interval);
	device_remove_file(led_cdev->dev, &dev_attr_count);
	device_remove_file(led_cdev->dev, &dev_attr_repeat);
	device_remove_file(led_cdev->dev, &dev_attr_raw);
	device_remove_file(led_cdev->dev, &dev_attr_message);

	morse_encoding_cleanup(&data->menc);
	kfree(data);

	led_cdev->trigger_data = NULL;
}

static struct led_trigger morse_led_trigger = {
	.name = "morse",
	.activate = morse_trig_activate,
	.deactivate = morse_trig_deactivate,
};

static int __init morse_trig_init(void)
{
	return led_trigger_register(&morse_led_trigger);
}

static void __exit morse_trig_exit(void)
{
	led_trigger_unregister(&morse_led_trigger);
}

module_init(morse_trig_init);
module_exit(morse_trig_exit);

MODULE_AUTHOR("Gaël Portay <gael.portay@gmail.com>");
MODULE_DESCRIPTION("Morse LED trigger");
MODULE_LICENSE("GPL");
