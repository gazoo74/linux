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

#ifndef __LINUX_MORSE_DECODING_H_INCLUDED
#define __LINUX_MORSE_DECODING_H_INCLUDED

#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/types.h>

struct cbuffer {
	char data[64];
	size_t first;
	size_t last;
};

static ssize_t cbuffer_size(struct cbuffer *cbuf)
{
	if (!cbuf)
		return -EINVAL;

	if (cbuf->last >= cbuf->first)
		return cbuf->last - cbuf->first;
	else
		return sizeof(cbuf->data) + cbuf->last - cbuf->first;
}

static bool cbuffer_empty(struct cbuffer *cbuf)
{
	if (!cbuf)
		return 0;

	return cbuf->first == cbuf->last;
}

static bool cbuffer_full(struct cbuffer *cbuf)
{
	if (!cbuf)
		return 0;

	return cbuf->first != cbuf->last;
}

extern ssize_t cbuffer_read(struct cbuffer *cbuf, char *buf, size_t size, bool peek);
extern ssize_t cbuffer_append_char(struct cbuffer *cbuf, char c);
extern ssize_t cbuffer_append_string(struct cbuffer *cbuf, char *buf, size_t size);
extern void cbuffer_hexdump(struct cbuffer *cbuf, const char *prefix, bool ascii);

struct ctx {
	struct edge {
		ktime_t time;
		bool val;
	} stack[30];
	size_t size;
};

struct morse_decoding {
	spinlock_t lock;
	ktime_t intra_gap;
	ktime_t letter_gap;
	ktime_t word_gap;

	ktime_t sync;
	ktime_t last;
	ktime_t min_srate;
	ktime_t max_srate;
	ktime_t min_3srate;
	ktime_t max_3srate;
	ktime_t min_4srate;
	ktime_t max_4srate;
	ktime_t min_7srate;
	ktime_t max_7srate;
	ktime_t min_8srate;
	ktime_t max_8srate;

	bool gap;
	char *message;
	const char *mess;
	const char *code;

	ktime_t prev;

	struct ctx ctx;

	struct cbuffer cbuf;
	char buf[6];
	size_t size;
};

extern void morse_decoding_init(struct morse_decoding *data);
extern void morse_decoding_cleanup(struct morse_decoding *data);
extern size_t morse_decoding_read(struct morse_decoding *data, char *buf, size_t size);
extern void morse_decoding_change(struct morse_decoding *data, bool state);

#endif /* __LINUX_MORSE_DECODING_H_INCLUDED */
