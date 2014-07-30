/*
 * Linux Kernel module for Morse decoding
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

#include "linux/morse-decoding.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

void cbuffer_hexdump(struct cbuffer *cbuf, const char *prefix, bool ascii)
{
	if (!cbuf)
		return;

	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_ADDRESS, 16, 1,
		       cbuf->data, sizeof(cbuf->data), ascii);
}

ssize_t cbuffer_read(struct cbuffer *cbuf, char *buf, size_t size, bool peek)
{
	ssize_t ret;
	ssize_t s;

	if (!cbuf)
		return -EINVAL;

	ret = MIN(cbuffer_size(cbuf), size - 1);
	if (cbuf->last >= cbuf->first) {
		memcpy(buf, &cbuf->data[cbuf->first], ret);
		if (!peek)
			cbuf->first += ret;
	}
	else {
		s = sizeof(cbuf->data) - cbuf->first;
		memcpy(buf, &cbuf->data[cbuf->first], s);
		memcpy(&buf[s], cbuf->data, cbuf->last);
		if (!peek && (ret < s))
		    cbuf->first += ret;
		else
		    cbuf->first = ret - s;
	}

	return ret;
}

ssize_t cbuffer_append_char(struct cbuffer *cbuf, char c)
{
    char buf[2] = { c, 0 };
    buf[0] = c;
    buf[1] = 0;
    return cbuffer_append_string(cbuf, buf, sizeof(buf));
}

ssize_t cbuffer_append_string(struct cbuffer *cbuf, char *buf, size_t size)
{
	ssize_t ret;
	ssize_t s;

	if (!cbuf)
		return -EINVAL;

	ret = MIN(sizeof(cbuf->data) - cbuffer_size(cbuf) - 1, size);
	if (cbuf->last < cbuf->first) {
		memcpy(&cbuf->data[cbuf->last], buf, ret);
		cbuf->last += ret;
	}
	else {
		s = sizeof(cbuf->data) - cbuf->last;
		if ((ret - s) < 0) {
			memcpy(&cbuf->data[cbuf->last], buf, ret);
			cbuf->last += ret;
		}
		else {
			memcpy(&cbuf->data[cbuf->last], buf, s);
			memcpy(cbuf->data, &buf[s], ret - s);
			cbuf->last = ret-s;
		}
	}

	cbuf->data[cbuf->last] = 0;

	return ret;
}

inline ssize_t ctx_push(struct ctx *ctx, const struct edge *elt)
{
	if (!ctx || !elt)
		return -EINVAL;

	if (ctx->size >= sizeof(ctx->stack))
		return -ENOMEM;

	ctx->stack[ctx->size].time = elt->time;
	ctx->stack[ctx->size].val = elt->val;
	ctx->size++;

	return ctx->size;
}

inline ssize_t ctx_pop(struct ctx *ctx, struct edge *elt)
{
	if (!ctx || !elt)
		return -EINVAL;

	if (ctx->size == 0)
		return -ENOMEM;

	*elt = ctx->stack[ctx->size];
	ctx->size--;

	return ctx->size;
}

inline ssize_t ctx_top(const struct ctx *ctx, struct edge *elt)
{
	if (!ctx || !elt)
		return -EINVAL;

	if (ctx->size == 0)
		return -ENOMEM;

	*elt = ctx->stack[ctx->size];

	return ctx->size;
}

void morse_decoding_init(struct morse_decoding *data)
{
	if (!data)
		return;

	data->sync = ktime_set(0, 0);
	data->last = ktime_set(0, 0);
	data->min_srate = ktime_set(0, 300000000);
	data->max_srate = ktime_set(0, 366666666);
	data->min_3srate = ktime_set(0, 900000000);
	data->max_3srate = ktime_set(1, 100000000);
	data->min_4srate = ktime_set(0, 199999999);
	data->max_4srate = ktime_set(1, 466666666);
	data->min_7srate = ktime_set(2, 99999999);
	data->max_7srate = ktime_set(2, 566666666);
	data->min_8srate = ktime_set(2, 399999999);
	data->max_8srate = ktime_set(2, 933333333);

	data->message = NULL;
	data->gap = 0;
	data->mess = NULL;
	data->code = NULL;

	data->intra_gap = ktime_set(0, 0);
	data->letter_gap = ktime_set(0, 0);
	data->word_gap = ktime_set(0, 0);

	data->prev = ktime_set(0, 0);

	data->cbuf.first = 0;
	data->cbuf.last = 0;
	data->size = 0;
}

void morse_decoding_cleanup(struct morse_decoding *data)
{
	unsigned long flags;
	u8 *oldmess;

	if (!data)
		return;

	oldmess = data->message;

	spin_lock_irqsave(&data->lock, flags);

	data->cbuf.last = 0;
	data->cbuf.first = 0;

	data->prev = ktime_set(0, 0);

	data->word_gap = ktime_set(0, 0);
	data->letter_gap = ktime_set(0, 0);
	data->intra_gap = ktime_set(0, 0);

	data->code = NULL;
	data->mess = NULL;
	data->gap = 0;
	data->message = NULL;

	spin_unlock_irqrestore(&data->lock, flags);

	kfree(oldmess);
}

size_t morse_decoding_read(struct morse_decoding *data, char *buf, size_t size)
{
	unsigned long flags;
	size_t s = 0;

	if (!data || !buf)
		return -EINVAL;

	spin_lock_irqsave(&data->lock, flags);
	s = cbuffer_read(&data->cbuf, buf, size, 0);
	spin_unlock_irqrestore(&data->lock, flags);

	return s;
}

char decode(struct morse_decoding *data, const ktime_t t)
{
	char ret;

	if ((ktime_compare(data->min_srate, t) <= 0) && (ktime_compare(t, data->max_srate) <= 0))
		ret = '.';
	else if ((ktime_compare(data->min_3srate, t) <= 0) && (ktime_compare(t, data->max_3srate) <= 0))
		ret = '-';
	else if ((ktime_compare(data->min_4srate, t) <= 0) && (ktime_compare(t, data->max_4srate) <= 0))
		ret = '-';
	else if ((ktime_compare(data->min_7srate, t) <= 0) && (ktime_compare(t, data->max_7srate) <= 0))
		ret = ' ';
	else if ((ktime_compare(data->min_8srate, t) <= 0) && (ktime_compare(t, data->max_8srate) <= 0))
		ret = ' ';
	else
		ret = '?';

	return ret;
}

void morse_decoding_change(struct morse_decoding *data, bool val)
{
	char c;
	ktime_t t;

	if (!data->last.tv64) {
	    data->last = ktime_sub(ktime_get(), data->min_srate);
	    return;
	}

	t = ktime_sub(ktime_get(), data->last);
	data->last = ktime_get();

	c = decode(data, t);

	if (!val) {
		if (data->size >= sizeof(data->buf))
		    return;

		data->size++;

		if (c == '.')
			return;

		data->buf[data->size] = 0;
		data->size = 0;
	}
	else
		data->buf[data->size] = c;
}

static int __init morse_decoding_mod_init(void)
{
	return 0;
}

static void __exit morse_decoding_mod_exit(void)
{

}

module_init(morse_decoding_mod_init);
module_exit(morse_decoding_mod_exit);

EXPORT_SYMBOL(morse_decoding_init);
EXPORT_SYMBOL(morse_decoding_cleanup);
EXPORT_SYMBOL(morse_decoding_read);
EXPORT_SYMBOL(morse_decoding_change);

MODULE_AUTHOR("Gaël Portay <gael.portay@gmail.com>");
MODULE_DESCRIPTION("Morse decoding");
MODULE_LICENSE("GPL");
