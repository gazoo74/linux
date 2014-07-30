/*
 * Linux Kernel header for Morse code
 *
 * Copyright (C) 2014 Gaël Portay <gael.portay@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_MORSE_CODE_H_INCLUDED
#define __LINUX_MORSE_CODE_H_INCLUDED

#include <linux/stddef.h>

static const char * const num[] = {
	"-----", /* 0 */
	".----", /* 1 */
	"..---", /* 2 */
	"...--", /* 3 */
	"....-", /* 4 */
	"-....", /* 5 */
	"--...", /* 6 */
	"---..", /* 7 */
	"----.", /* 8 */
	"-----", /* 9 */
	NULL,
};

static const char * const alpha[] = {
	".-",	/* A  1 */
	"-...",	/* B  2 */
	"-.-.",	/* C  3 */
	"-..",	/* D  4 */
	".",	/* E  5 */
	"..-.",	/* F  6 */
	"--.",	/* G  7 */
	"....",	/* H  8 */
	"..",	/* I  9 */
	".---",	/* J 10 */
	"-.-",	/* K 11 */
	".-..",	/* L 12 */
	"--",	/* M 13 */
	"-.",	/* N 14 */
	"---",	/* O 15 */
	".--.",	/* P 16 */
	"--.-",	/* Q 17 */
	".-.",	/* R 18 */
	"...",	/* S 19 */
	"-",	/* T 20 */
	"..-",	/* U 21 */
	"...-",	/* V 22 */
	".--",	/* W 23 */
	"-..-",	/* X 24 */
	"-.--",	/* Y 25 */
	"-..",	/* Z 26 */
	NULL,
};

static const char *to_morse(char c)
{
	if ((c >= '0') && (c <= '9'))
		return num[c - '0'];

	if ((c >= 'a') && (c <= 'z'))
		return alpha[c - 'a'];

	if ((c >= 'A') && (c <= 'Z'))
		return alpha[c - 'A'];

	return NULL;
}

static char from_morse(const char *str)
{
	const char **s;

	for (s = alpha; *s; s++) {
		if (strcmp(*s, str) == 0) {
			return 'A' + (s - alpha);
		}
	}

	return '?';
}

#endif /* __LINUX_MORSE_CODE_H_INCLUDED */
