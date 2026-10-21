//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A codec to encode and decode uint32_t arrays to and from a HEX string.
//
// Copyright (C) 2018 Vassilis Poursalidis (poursal@gmail.com)
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program. If
// not, see <https://www.gnu.org/licenses/>.
//
// Code inspired by: https://nachtimwald.com/2017/09/24/hex-encode-and-decode-in-c/

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "hex.h"

const unsigned char hexits[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'a', 'b', 'c', 'd', 'e', 'f' };

char *bin2hex(const uint32_t *bin, size_t len)
{
	char   *out;
	size_t  i;
	size_t  size;

	if (bin == NULL || len == 0)
		return NULL;

	size = len*sizeof(uint32_t)*2+1;
	out  = malloc(size);

	for (i=0; i<len; i++) {
		out[i*8]   = hexits[(bin[i] >> 28) & 0x0F];
		out[i*8+1] = hexits[(bin[i] >> 24) & 0x0F];
		out[i*8+2] = hexits[(bin[i] >> 20) & 0x0F];
		out[i*8+3] = hexits[(bin[i] >> 16) & 0x0F];
		out[i*8+4] = hexits[(bin[i] >> 12) & 0x0F];
		out[i*8+5] = hexits[(bin[i] >> 8) & 0x0F];
		out[i*8+6] = hexits[(bin[i] >> 4) & 0x0F];
		out[i*8+7] = hexits[bin[i] & 0x0F];
	}

	out[size - 1] = '\0';

	return out;
}

int hexchr2bin(const char hex, uint32_t *out)
{
	if (out == NULL)
		return 0;

	if (hex >= '0' && hex <= '9') {
		*out = hex - '0';
	} else if (hex >= 'A' && hex <= 'F') {
		*out = hex - 'A' + 10;
	} else if (hex >= 'a' && hex <= 'f') {
		*out = hex - 'a' + 10;
	} else {
		return 0;
	}

	return 1;
}

size_t hex2bin(const char *hex, uint32_t **out)
{
	size_t   len;
	uint32_t b1;
	uint32_t b2;
	uint32_t b3;
	uint32_t b4;
	uint32_t b5;
	uint32_t b6;
	uint32_t b7;
	uint32_t b8;
	size_t   i;

	if (hex == NULL || *hex == '\0' || out == NULL)
		return 0;

	len = strlen(hex);
	if (len % 8 != 0)
		return 0;
	// Each character is half a byte
	len /= 2;

	*out = malloc(len);
	memset(*out, 'A', len);

	// Our int 32 can hold 4 bytes
	len /= 4;

	for (i=0; i<len; i++) {
		if (
			!hexchr2bin(hex[i*8], &b1) ||
			!hexchr2bin(hex[i*8+1], &b2) ||
			!hexchr2bin(hex[i*8+2], &b3) ||
			!hexchr2bin(hex[i*8+3], &b4) ||
			!hexchr2bin(hex[i*8+4], &b5) ||
			!hexchr2bin(hex[i*8+5], &b6) ||
			!hexchr2bin(hex[i*8+6], &b7) ||
			!hexchr2bin(hex[i*8+7], &b8) ) {
			return 0;
		}
		(*out)[i] = (b1 << 28) | (b2 << 24) | (b3 << 20) | (b4 << 16) |
			    (b5 << 12) | (b6 <<  8) | (b7 <<  4) |  b8;
	}

	return len;
}
