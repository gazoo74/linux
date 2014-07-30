/*
 * LED Sequence Trigger
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
#include <linux/hrtimer.h>
#include <linux/string.h>

#include "../leds.h"

#define timer_to_sequence(timer) \
	container_of(timer, struct sequence_trig_data, timer)

#define DEFAULT_INVERVAL 10000000
#define DEFAULT_COUNT 0
#define DELIMITER 0x0a

struct sequence_trig_data {
	struct led_classdev *cdev;
	spinlock_t lock;
	struct hrtimer timer;
	ktime_t interval;
	unsigned int plot_index;
	size_t plot_count;
	u8 *plot;
	unsigned int sequence_count;
	unsigned int sequence_repeat;
};

static void sequence_start(struct sequence_trig_data *data)
{
	unsigned long flags;

	if (hrtimer_active(&data->timer))
		return;

	spin_lock_irqsave(&data->lock, flags);
	data->plot_index = 0;
	data->sequence_count = 0;
	hrtimer_start(&data->timer, ktime_get(), HRTIMER_MODE_ABS);
	spin_unlock_irqrestore(&data->lock, flags);
}

static void sequence_stop(struct sequence_trig_data *data)
{
	unsigned long flags;

	if (!hrtimer_active(&data->timer))
		return;

	spin_lock_irqsave(&data->lock, flags);
	data->plot_index = 0;
	data->sequence_count = 0;
	hrtimer_cancel(&data->timer);
	spin_unlock_irqrestore(&data->lock, flags);
}

static void sequence_reset(struct sequence_trig_data *data)
{
	data->plot_index = 0;
	data->sequence_count = 0;
}

static void sequence_pause(struct sequence_trig_data *data)
{
	unsigned long flags;

	if (!hrtimer_active(&data->timer))
		return;

	spin_lock_irqsave(&data->lock, flags);
	hrtimer_cancel(&data->timer);
	spin_unlock_irqrestore(&data->lock, flags);
}

static void sequence_resume(struct sequence_trig_data *data)
{
	unsigned long flags;

	if (hrtimer_active(&data->timer))
		return;

	spin_lock_irqsave(&data->lock, flags);
	hrtimer_start(&data->timer, ktime_get(), HRTIMER_MODE_ABS);
	spin_unlock_irqrestore(&data->lock, flags);
}

static enum hrtimer_restart led_sequence_function(struct hrtimer *timer)
{
	struct sequence_trig_data *data = timer_to_sequence(timer);
	struct led_classdev *cdev = data->cdev;
	enum hrtimer_restart ret = HRTIMER_RESTART;
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	if (data->plot) {
		led_set_brightness(cdev, data->plot[data->plot_index]);
		data->plot_index++;
		if (data->plot_index >= data->plot_count) {
			data->plot_index = 0;
			data->sequence_count++;
			if (data->sequence_repeat &&
			    data->sequence_count >= data->sequence_repeat)
				ret = HRTIMER_NORESTART;
		}
	}
	spin_unlock_irqrestore(&data->lock, flags);

	if (ret == HRTIMER_RESTART)
		hrtimer_add_expires(timer, data->interval);

	return ret;
}

static ssize_t sequence_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;

	return snprintf(buf, PAGE_SIZE, "%sactive\n",
			hrtimer_active(&data->timer) ? "" : "in");
}

static DEVICE_ATTR(status, 0444, sequence_status_show, NULL);

static ssize_t sequence_control_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "start stop reset pause resume\n");
}

static ssize_t sequence_control_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;

	if (strncasecmp(buf, "start", sizeof("start") - 1) == 0)
		sequence_start(data);
	else if (strncasecmp(buf, "stop", sizeof("stop") - 1) == 0)
		sequence_stop(data);
	else if (strncasecmp(buf, "reset", sizeof("reset") - 1) == 0)
		sequence_reset(data);
	else if (strncasecmp(buf, "pause", sizeof("stop") - 1) == 0)
		sequence_pause(data);
	else if (strncasecmp(buf, "resume", sizeof("resume") - 1) == 0)
		sequence_resume(data);
	else
		return -EINVAL;

	return size;
}

static DEVICE_ATTR(control, 0644, sequence_control_show,
		   sequence_control_store);

static ssize_t sequence_repeat_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;

	return snprintf(buf, PAGE_SIZE, "%u\n", data->sequence_repeat);
}

static ssize_t sequence_repeat_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long count;
	int err;

	err = kstrtoul(buf, 0, &count);
	if (err)
		return -EINVAL;

	data->sequence_repeat = count;

	return size;
}

static DEVICE_ATTR(repeat, 0644, sequence_repeat_show, sequence_repeat_store);

static ssize_t sequence_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;

	return snprintf(buf, PAGE_SIZE, "%d\n", data->sequence_count);
}

static DEVICE_ATTR(count, 0444, sequence_count_show, NULL);

static ssize_t sequence_interval_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long interval = div_s64(ktime_to_ns(data->interval), 1000000);

	return snprintf(buf, PAGE_SIZE, "%lu\n", interval);
}

static ssize_t sequence_interval_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long interval;
	int err;

	err = kstrtoul(buf, 0, &interval);
	if (err)
		return -EINVAL;

	data->interval = ktime_set(interval / 1000,
				   (interval % 1000) * 1000000);

	return size;
}

static DEVICE_ATTR(interval, 0644, sequence_interval_show,
		sequence_interval_store);

static ssize_t sequence_rawplot_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	if (data->plot)
		memcpy(buf, data->plot, data->plot_count);
	spin_unlock_irqrestore(&data->lock, flags);

	return data->plot_count;
}

static ssize_t sequence_rawplot_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long flags;
	u8 *plot;
	u8 *oldplot = NULL;

	plot = kzalloc(size, GFP_KERNEL);
	if (!plot)
		return -ENOMEM;

	hrtimer_cancel(&data->timer);

	memcpy(plot, buf, size);

	spin_lock_irqsave(&data->lock, flags);
	if (data->plot)
		oldplot = data->plot;
	data->plot = plot;
	data->plot_index = 0;
	data->plot_count = size;
	spin_unlock_irqrestore(&data->lock, flags);

	kfree(oldplot);

	hrtimer_start(&data->timer, ktime_get(), HRTIMER_MODE_ABS);

	return data->plot_count;
}

static DEVICE_ATTR(rawplot, 0644, sequence_rawplot_show,
		sequence_rawplot_store);

static ssize_t sequence_plot_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long flags;
	int i;
	size_t size = 0;

	spin_lock_irqsave(&data->lock, flags);
	if (data->plot)
		for (i = 0; i < data->plot_count; i++)
			size += snprintf(&buf[size], PAGE_SIZE - size, "%u%c",
					 data->plot[i], DELIMITER);
	spin_unlock_irqrestore(&data->lock, flags);

	return size;
}

static ssize_t sequence_plot_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long flags;
	ssize_t count;
	const char *del;

	count = 0;
	del = buf;
	for (;;) {
		del = strchr(del, DELIMITER);
		if (!del)
			break;
		del++;
		count++;
	}

	if (count) {
		int i = 0;
		char str[4];
		const char *ptr;
		u8 *oldplot = NULL;
		u8 *plot = kzalloc(size, GFP_KERNEL);

		if (!plot)
			return -ENOMEM;

		ptr = buf;
		del = buf;
		for (;;) {
			int err;
			unsigned long val = 0;

			del = strchr(ptr, DELIMITER);
			if (!del)
				break;

			err = (del - ptr);
			if (err >= sizeof(str)) {
				kfree(plot);
				return -EINVAL;
			}

			strncpy(str, ptr, err);
			str[err] = 0;
			err = kstrtoul(str, 0, &val);
			if (err || (val > LED_FULL)) {
				kfree(plot);
				return (err < 0) ? err : -EINVAL;
			}

			plot[i] = val;
			ptr = del + 1;
			i++;
		}

		hrtimer_cancel(&data->timer);

		spin_lock_irqsave(&data->lock, flags);
		if (data->plot)
			oldplot = data->plot;
		data->plot = plot;
		data->plot_index = 0;
		data->plot_count = count;
		spin_unlock_irqrestore(&data->lock, flags);

		kfree(oldplot);

		hrtimer_start(&data->timer, ktime_get(), HRTIMER_MODE_ABS);
	}

	return size;
}

static DEVICE_ATTR(plot, 0644, sequence_plot_show,
		sequence_plot_store);

static void sequence_trig_activate(struct led_classdev *led_cdev)
{
	struct sequence_trig_data *data;

	data = kzalloc(sizeof(struct sequence_trig_data), GFP_KERNEL);
	if (!data)
		return;

	led_cdev->trigger_data = data;

	spin_lock_init(&data->lock);

	data->cdev = led_cdev;
	data->interval = ktime_set(0, DEFAULT_INVERVAL);
	data->sequence_count = 0;
	data->sequence_repeat = DEFAULT_COUNT;

	data->plot_index = 0;
	data->plot_count = (LED_FULL * 2);
	data->plot = kzalloc(data->plot_count, GFP_KERNEL);
	if (data->plot) {
		int i;
		int val = LED_FULL;
		int step = 1;

		for (i = 0; i < data->plot_count; i++) {
			data->plot[i] = val;
			if (val == LED_FULL)
				step = -1;
			else if (val == LED_OFF)
				step = 1;
			val += step;
		}

		hrtimer_init(&data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
		data->timer.function = led_sequence_function;
	}

	device_create_file(led_cdev->dev, &dev_attr_status);
	device_create_file(led_cdev->dev, &dev_attr_control);
	device_create_file(led_cdev->dev, &dev_attr_repeat);
	device_create_file(led_cdev->dev, &dev_attr_count);
	device_create_file(led_cdev->dev, &dev_attr_interval);
	device_create_file(led_cdev->dev, &dev_attr_rawplot);
	device_create_file(led_cdev->dev, &dev_attr_plot);
}

static void sequence_trig_deactivate(struct led_classdev *led_cdev)
{
	struct sequence_trig_data *data = led_cdev->trigger_data;
	unsigned long flags;
	u8 *plot;

	device_remove_file(led_cdev->dev, &dev_attr_status);
	device_remove_file(led_cdev->dev, &dev_attr_control);
	device_remove_file(led_cdev->dev, &dev_attr_repeat);
	device_remove_file(led_cdev->dev, &dev_attr_count);
	device_remove_file(led_cdev->dev, &dev_attr_interval);
	device_remove_file(led_cdev->dev, &dev_attr_rawplot);
	device_remove_file(led_cdev->dev, &dev_attr_plot);

	if (data) {
		hrtimer_cancel(&data->timer);

		spin_lock_irqsave(&data->lock, flags);
		plot = data->plot;
		if (data->plot) {
			data->plot = NULL;
			data->plot_index = 0;
			data->plot_count = 0;
		}
		spin_unlock_irqrestore(&data->lock, flags);

		kfree(plot);

		kfree(data);
	}
}

static struct led_trigger sequence_led_trigger = {
	.name = "sequence",
	.activate = sequence_trig_activate,
	.deactivate = sequence_trig_deactivate,
};

static int __init sequence_trig_init(void)
{
	return led_trigger_register(&sequence_led_trigger);
}

static void __exit sequence_trig_exit(void)
{
	led_trigger_unregister(&sequence_led_trigger);
}

module_init(sequence_trig_init);
module_exit(sequence_trig_exit);

MODULE_AUTHOR("Gaël Portay <g.portay@overkiz.com>");
MODULE_DESCRIPTION("Sequence LED trigger");
MODULE_LICENSE("GPL");
