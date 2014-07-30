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
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/morse-encoding.h>
#include <linux/morse-decoding.h>

#include "../leds.h"

#define timer_to_morse_trig(timer) \
	container_of(timer, struct morse_trig_data, timer)

struct morse_trig_data {
	struct led_classdev *cdev;
	struct morse_encoding menc;
	spinlock_t lock;
	wait_queue_head_t wait;
	char devname[32];
	struct miscdevice miscdev;
	struct morse_decoding mdec;
};

static int morse_trig_function(struct morse_trig_data *data, bool on)
{
	static bool prev = 0;
	struct led_classdev *led_cdev = data->cdev;

	__led_set_brightness(led_cdev, on ? led_cdev->max_brightness : LED_OFF);

	if (prev != on) {
		morse_decoding_change(&data->mdec, !on);
	}
	else {
		printk(KERN_WARNING "Previously %s!\n", prev ? "ON" : "OFF");
	}

	prev = on;
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

static unsigned int ledtrig_morse_poll(struct file *file,
				       struct poll_table_struct *wait)
{
	struct miscdevice *misc = file->private_data;
	struct morse_trig_data *data = container_of(misc,
						    struct morse_trig_data,
						    miscdev);
	unsigned int mask = 0;
	unsigned long flags;

	printk(KERN_INFO "%s:%u file: %p\n", __FUNCTION__, __LINE__, file);

	spin_lock_irqsave(&data->lock, flags);
#if 0
	if (!list_empty(&wmbus->packets))
		mask |= POLLIN;

#endif
	spin_unlock_irqrestore(&data->lock, flags);
	poll_wait(file, &data->wait, wait);

	return mask;
}

static ssize_t ledtrig_morse_read(struct file *file, char __user *buf,
				  size_t len, loff_t *ppos)
{
	struct miscdevice *misc = file->private_data;
	struct morse_trig_data *data = container_of(misc,
						    struct morse_trig_data,
						    miscdev);
	struct cbuffer *cbuf = &data->mdec.cbuf;
	ssize_t ret = -EAGAIN;
	ssize_t size = 0;
	unsigned long flags;

	printk(KERN_INFO "%s:%u file: %p\n", __FUNCTION__, __LINE__, file);

	spin_lock_irqsave(&data->lock, flags);
#if 0
	while (!packet) {
		spin_lock_irqsave(&wmbus->lock, flags);
		if (!list_empty(&wmbus->packets)) {
			packet = list_first_entry(&wmbus->packets,
						  struct wmbus_packet, node);
			list_del(&packet->node);
			wmbus->npackets--;
		}
		spin_unlock_irqrestore(&wmbus->lock, flags);
		if ((file->f_flags & O_NONBLOCK) || packet)
			break;
		ret = wait_event_interruptible(wmbus->wait,
					       !list_empty(&wmbus->packets));
		if (ret)
			break;
	}

	if (packet) {
		if (len > packet->size)
			len = packet->size;

		ret = copy_to_user(buf, packet->buffer, len);
		if (ret)
			ret = len - ret;
		else
			ret = len;
		devm_kfree(misc->this_device, packet);
	}
#else
	while (1) {
		ret = wait_event_interruptible(data->wait,
					       !cbuffer_empty(cbuf));

		if (ret)
			break;

		ret = 0;
		while (!cbuffer_empty(cbuf)) {
			ret = cbuffer_read(cbuf, buf, size , 0);
		}

		if ((file->f_flags & O_NONBLOCK))
			break;
	}
#endif
	spin_unlock_irqrestore(&data->lock, flags);

	return ret;
}

static int ledtrig_morse_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "%s:%u inode: %li\n", __FUNCTION__, __LINE__, inode->i_ino);
	return 0;
}

static int ledtrig_morse_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "%s:%u inode: %li\n", __FUNCTION__, __LINE__, inode->i_ino);
	return 0;
}

static const struct file_operations ledtrig_morse_fops = {
	.owner = THIS_MODULE,
	.read = ledtrig_morse_read,
	.poll = ledtrig_morse_poll,
	.open = ledtrig_morse_open,
	.release = ledtrig_morse_release,
};

static void morse_trig_activate(struct led_classdev *led_cdev)
{
	struct morse_trig_data *data;
	int err;

	printk(KERN_INFO "%s:%u %s\n", __FUNCTION__, __LINE__, __TIMESTAMP__);

	data = kzalloc(sizeof(struct morse_encoding), GFP_KERNEL);
	if (!data)
		return;

	led_cdev->trigger_data = data;

	data->cdev = led_cdev;

	morse_encoding_init(&data->menc, data,
			    (int (*) (void *, bool)) morse_trig_function);
	morse_decoding_init(&data->mdec);

	device_create_file(led_cdev->dev, &dev_attr_message);
	device_create_file(led_cdev->dev, &dev_attr_raw);
	device_create_file(led_cdev->dev, &dev_attr_repeat);
	device_create_file(led_cdev->dev, &dev_attr_count);
	device_create_file(led_cdev->dev, &dev_attr_interval);

	spin_lock_init(&data->lock);
	printk(KERN_INFO "%s:%u led_cdev->name: %s (%i)\n", __FUNCTION__, __LINE__, led_cdev->name, strlen(led_cdev->name));
	snprintf(data->devname, sizeof(data->devname), "%s", led_cdev->name);
	printk(KERN_INFO "%s:%u data->devname: %s\n", __FUNCTION__, __LINE__, data->devname);
	data->miscdev.name = data->devname;
	data->miscdev.fops = &ledtrig_morse_fops;
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.parent = led_cdev->dev;
	data->miscdev.this_device = NULL;

	err = misc_register(&data->miscdev);
	if (err)
		printk(KERN_ERR "Cannot register misc-device %s! (errno: %i)\n", data->devname, err);
}

static void morse_trig_deactivate(struct led_classdev *led_cdev)
{
	struct morse_trig_data *data = led_cdev->trigger_data;
	int err;

	printk(KERN_INFO "%s:%u\n", __FUNCTION__, __LINE__);

	err = misc_deregister(&data->miscdev);
	if (err)
		printk(KERN_ERR "Cannot deregister misc-device %s! (errno: %i)\n", data->devname, err);

	*data->devname = NULL;

	device_remove_file(led_cdev->dev, &dev_attr_interval);
	device_remove_file(led_cdev->dev, &dev_attr_count);
	device_remove_file(led_cdev->dev, &dev_attr_repeat);
	device_remove_file(led_cdev->dev, &dev_attr_raw);
	device_remove_file(led_cdev->dev, &dev_attr_message);

	morse_decoding_cleanup(&data->mdec);
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
