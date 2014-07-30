/*
 * Linux Kernel module for Morse encoding
 *
 * Copyright (C) 2014 Gaël Portay <gael.portay@gmail.com>
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
#include <linux/string.h>
#include <linux/morse-code.h>

#include "linux/morse-encoding.h"

#define RELEASE
#ifdef RELEASE
#define DEBUG(str, ...)
#else
#define DEBUG(str, ...) printk(str, __VA_ARGS__)
#endif

#define timer_to_morse_encoding(timer) \
	container_of(timer, struct morse_encoding, timer)

static enum hrtimer_restart
timer_morse_encoding_function(struct hrtimer *timer)
{
	unsigned long flags;
	struct morse_encoding *data = timer_to_morse_encoding(timer);
	enum hrtimer_restart ret = HRTIMER_NORESTART;
	bool on = 0;
	ktime_t time = { .tv64 = 0 };

	spin_lock_irqsave(&data->lock, flags);
	DEBUG(KERN_INFO "%s:%03u >>> data: { context: %i, (%-5s:%-5s), %3i }\n", __FUNCTION__, __LINE__, data->gap, data->mess, data->code, data->count);

	if (!data || !data->message ||
	    ((data->repeat > 0) && (data->count >= data->repeat)))
		goto exit;

	ret = HRTIMER_RESTART;

	if (data->gap) {
		if (!data->code || (*data->code == 0)) {
			if (!data->mess || (*data->mess == 0) ||
			    (*data->mess == ' '))
				time = data->word_gap;
			else
				time = data->letter_gap;
		} else {
			time = data->intra_gap;
		}
		data->gap = !data->gap;
		goto exit;
	}

	if (!data->code || (*data->code == 0)) {
		if (!data->mess || (*data->mess == 0)) {
			data->count++;
			data->mess = data->message;
		}

		data->code = to_morse(*data->mess);
		data->mess++;
	}

	if (!data->code) {
		printk(KERN_ERR "Not suppose to happen! { message: %s, code: %s, time: %i/%lli }\n", data->message, data->code, on, time.tv64);
		time = data->word_gap;
	}
	else {
		time = *data->code == '.' ? data->intra_gap : data->letter_gap;
	}

	data->gap = !data->gap;
	on = !on;
	data->code++;

exit:
	DEBUG(KERN_INFO "%s:%03u <<< data: { context: %i, (%-5s:%-5s), %3i }, local: { ret: (%i/%i), on: %i, time: %lli }\n", __FUNCTION__, __LINE__, data->gap, data->mess, data->code, data->count, ret, halted, on, time.tv64);
	spin_unlock_irqrestore(&data->lock, flags);

	if (time.tv64 && data->callback) {
		if (data->callback(data->user_data, on)) {
			DEBUG(KERN_INFO "%s:%03u abort\n", __FUNCTION__, __LINE__);
			ret =  HRTIMER_NORESTART;
		}
	}
	else {
		DEBUG(KERN_INFO "Not calling callback (time: %lli)\n", time.tv64);
	}

	if (!time.tv64) {
		DEBUG(KERN_INFO "%s:%03u time is %lli and %s\n", __FUNCTION__, __LINE__, time.tv64, ret == HRTIMER_RESTART ? "restarts" : "no-restarts");
		return ret;
	}

	if (ret == HRTIMER_RESTART) {
		hrtimer_add_expires(timer, time.tv64 ? time : ktime_get());
	}
	else {
		DEBUG(KERN_ERR "%s:%03u message: %s\n", __FUNCTION__, __LINE__, data->message);
		DEBUG(KERN_ERR "%s:%03u code:    %s\n", __FUNCTION__, __LINE__, data->code);
		DEBUG(KERN_ERR "%s:%03u mess:    %s\n", __FUNCTION__, __LINE__, data->mess);
	}

	DEBUG(KERN_ERR "%s:%03u\n", __FUNCTION__, __LINE__);
	DEBUG(KERN_ERR "%s:%03u\n", __FUNCTION__, __LINE__);
	DEBUG(KERN_ERR "%s:%03u\n", __FUNCTION__, __LINE__);

	return ret;
}

void morse_encoding_init(struct morse_encoding *data, void *user,
			 int (*cb)(void *, bool))
{
	if (!data)
		return;

	spin_lock_init(&data->lock);
	hrtimer_init(&data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	data->timer.function = timer_morse_encoding_function;

	data->user_data = user;
	data->callback = cb;

	data->message = NULL;
	data->gap = 0;
	data->mess = NULL;
	data->code = NULL;

	data->count = 0;
	data->repeat = 0;

	data->intra_gap = ktime_set(0, 333333333);
	data->letter_gap = ktime_set(1, 000000000);
	data->word_gap = ktime_set(2, 333333333);
//	data->intra_gap = ktime_set(0, 500000000);
//	data->letter_gap = ktime_set(1, 500000000);
//	data->word_gap = ktime_set(3, 500000000);
}
EXPORT_SYMBOL(morse_encoding_init);

void morse_encoding_cleanup(struct morse_encoding *data)
{
	unsigned long flags;
	u8 *oldmess;

	if (!data)
		return;

	oldmess = data->message;

	spin_lock_irqsave(&data->lock, flags);
	hrtimer_cancel(&data->timer);

	data->word_gap = ktime_set(0, 0);
	data->letter_gap = ktime_set(0, 0);
	data->intra_gap = ktime_set(0, 0);

	data->callback = NULL;
	data->user_data = NULL;

	data->code = NULL;
	data->mess = NULL;
	data->gap = 0;
	data->message = NULL;

	data->repeat = 0;
	data->count = 0;

	data->timer.function = NULL;
	spin_unlock_irqrestore(&data->lock, flags);

	kfree(oldmess);
}
EXPORT_SYMBOL(morse_encoding_cleanup);

ssize_t morse_encoding_get_raw(struct morse_encoding *data, char *buf,
			       ssize_t size)
{
	const char *code;
	const char *m;
	ssize_t s = 0;

	if (!buf)
		return 0;

	if (!data->message) {
		*buf = 0;
		return 0;
	}

	for (m = data->message; *m != 0; m++) {
		code = to_morse(*m);
		if (*code == ' ') {
			buf[s++] = ' ';
			buf[s] = 0;
			continue;
		}

		s += snprintf(&buf[s], size - s, "%s/", code);
	}

	return s;
}
EXPORT_SYMBOL(morse_encoding_get_raw);

ssize_t morse_encoding_set_message(struct morse_encoding *data,
				   const char *mess)
{
	unsigned long flags;
	ssize_t size;
	char *newmess;
	const char *oldmess;

	if (!data || !mess)
		return -EINVAL;

	size = strlen(mess);
	newmess = kzalloc(size, GFP_KERNEL);

	if (!newmess)
		return -ENOMEM;

	strncpy(newmess, mess, size);
	oldmess = data->message;

	morse_encoding_stop(data);

	spin_lock_irqsave(&data->lock, flags);
	data->message = newmess;
	if (!oldmess) {
		data->gap = 0;
//		data->mess = data->message;
//		data->code = to_morse(*data->mess++);
		data->mess = NULL;
		data->code = NULL;
	} else {
		data->gap = 1;
		data->mess = &data->message[size];
		data->code = to_morse(*data->mess);
	}
	spin_unlock_irqrestore(&data->lock, flags);

	morse_encoding_start(data);

	kfree(oldmess);

	return size;
}
EXPORT_SYMBOL(morse_encoding_set_message);

void morse_encoding_set_repeat(struct morse_encoding *data,
			       unsigned int repeat)
{
	unsigned long flags;

	if (!data)
		return;

	spin_lock_irqsave(&data->lock, flags);
	data->repeat = repeat;
	if ((data->repeat == 0) || (data->count < repeat)) {
		morse_encoding_start(data);
	}
	spin_unlock_irqrestore(&data->lock, flags);
}
EXPORT_SYMBOL(morse_encoding_set_repeat);

void morse_encoding_set_timeunit_us(struct morse_encoding *data,
				    unsigned long timeunit_us)
{
	unsigned long flags;
	unsigned long l;

	if (!data)
		return;

	spin_lock_irqsave(&data->lock, flags);
	l = timeunit_us;
	data->intra_gap = ktime_set(l / 1000, (l % 1000) * 1000000);

	l = timeunit_us * 3;
	data->letter_gap = ktime_set(l / 1000, (l % 1000) * 1000000);

	l = timeunit_us * 7;
	data->word_gap = ktime_set(l / 1000, (l % 1000) * 1000000);
	spin_unlock_irqrestore(&data->lock, flags);
}
EXPORT_SYMBOL(morse_encoding_set_timeunit_us);

void morse_encoding_start(struct morse_encoding *data)
{
	unsigned long flags;

	if (!data || hrtimer_active(&data->timer))
		return;

	spin_lock_irqsave(&data->lock, flags);
	hrtimer_start(&data->timer, ktime_get(), HRTIMER_MODE_ABS);
	spin_unlock_irqrestore(&data->lock, flags);
}
EXPORT_SYMBOL(morse_encoding_start);

void morse_encoding_stop(struct morse_encoding *data)
{
	unsigned long flags;

	if (!data || !hrtimer_active(&data->timer))
		return;

	spin_lock_irqsave(&data->lock, flags);
	hrtimer_cancel(&data->timer);
	spin_unlock_irqrestore(&data->lock, flags);
}
EXPORT_SYMBOL(morse_encoding_stop);

void morse_encoding_reset(struct morse_encoding *data)
{
	unsigned long flags;

	if (!data)
		return;

	spin_lock_irqsave(&data->lock, flags);
	data->gap = 0;
	data->mess = data->message;
	data->code = to_morse(*data->mess);
	data->count = 0;
	spin_unlock_irqrestore(&data->lock, flags);
}
EXPORT_SYMBOL(morse_encoding_reset);

static int __init morse_encoding_mod_init(void)
{
	printk(KERN_INFO "%s:%u %s\n", __FUNCTION__, __LINE__, __TIMESTAMP__);
	return 0;
}

static void __exit morse_encoding_mod_exit(void)
{
	printk(KERN_INFO "%s:%u %s\n", __FUNCTION__, __LINE__, __TIMESTAMP__);
}

module_init(morse_encoding_mod_init);
module_exit(morse_encoding_mod_exit);

MODULE_AUTHOR("Gaël Portay <gael.portay@gmail.com>");
MODULE_DESCRIPTION("Morse encoding");
MODULE_LICENSE("GPL");
