//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Only the ROMix part of scrypt.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include "codec/hex.h"
#include "codec/base64.h"
#include "lib/util.h"
#include "lib/scrypt_lib.h"

//
// TODO: Support Base64 encoding and decoding
//
int main(int argc, char *argv[])
{
	uint32_t *bin;
	size_t    binlen;
	char     *hex;
	int       retval;

	if (argc != 3) {
		printf("ERROR: missing argument. Usage: scrypt_romix <hex or base64 input> <CPU/memory cost parameter>\n");
		return -1;
	}

	if ( only_digits(argv[2])==0 ) {
		printf("ERROR: The CPU/memory cost parameter must be a natural number (use 32768).\n");
		return -2;
	}

	binlen = hex2bin(argv[1], &bin);

	if ( binlen==0 ) {
		printf("ERROR: Not a valid HEX input value.\n");
		return -2;
	}

	retval = scrypt_romix(bin, binlen, atoi(argv[2]));

	if ( retval!=0 ) {
		printf("ERROR: Failed to execute ROMix. Do we have enough memory?\n");
		return -3;
	}

	hex = bin2hex(bin, binlen);

	printf("%s\n", hex);

	secure_wipe((uint8_t *)bin, sizeof(uint32_t)*binlen);
	secure_wipe((uint8_t *)hex, sizeof(uint32_t)*binlen*2); // Each byte is 2 chars long

	free(bin);
	free(hex);

	return 0;
}
