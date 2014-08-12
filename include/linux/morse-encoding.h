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

#ifndef __LINUX_MORSE_ENCODING_H_INCLUDED
#define __LINUX_MORSE_ENCODING_H_INCLUDED

#include <linux/hrtimer.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/types.h>

struct morse_encoding {
	struct hrtimer timer;
	spinlock_t lock;
	ktime_t intra_gap;
	ktime_t letter_gap;
	ktime_t word_gap;

	void *user_data;
	int (*callback)(void *, bool);

	bool gap;
	char *message;
	const char *mess;
	const char *code;

	unsigned int count;
	unsigned int repeat;
};

extern void morse_encoding_init(struct morse_encoding *data,
				void *user, int (*cb) (void *, bool));
extern void morse_encoding_cleanup(struct morse_encoding *data);
extern ssize_t morse_encoding_get_raw(struct morse_encoding *data,
				      char *buf, ssize_t size);
extern ssize_t morse_encoding_set_message(struct morse_encoding *data,
					  const char *mess);
extern void morse_encoding_set_repeat(struct morse_encoding *data,
				      unsigned int count);
extern void morse_encoding_set_timeunit_us(struct morse_encoding *data,
					   unsigned long timeunit_us);
extern void morse_encoding_start(struct morse_encoding *data);
extern void morse_encoding_stop(struct morse_encoding *data);
extern void morse_encoding_reset(struct morse_encoding *data);

#endif /* __LINUX_MORSE_ENCODING_H_INCLUDED */
