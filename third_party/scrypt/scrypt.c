//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The full scrypt implementation
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

/**
 * Code taken from http://bitwiseshiftleft.github.io/sjcl/
 * and transformed to work with C.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
// https://www.gnu.org/software/libunistring/
// https://www.gnu.org/software/libunistring/manual/html_node/Normalization-of-strings.html
// #include <uninorm.h>
#include "codec/hex.h"
#include "codec/base64.h"
#include "lib/util.h"
#include "lib/pbkdf2.h"
#include "lib/scrypt_lib.h"

#define  SCRYPT_SIZE_MAX  (uint32_t)4294967295

//
// TODO: Support Base64 encoding and decoding
//
int main(int argc, char **argv)
{
	// scrypt default params
	int       r = 8;
	int       p = 1;
	// local vars
	uint32_t *binpass;
	size_t    binpasslen;
	uint32_t *binsalt;
	size_t    binsaltlen;
	char     *hex;
	int       retval;
	int       outlen = p * (128/4) * r; // sjcl measures the output in bits => /8 for bytes /4 for 4 byte words
	uint32_t  out[outlen];
	int       finallen = 16;
	uint32_t  final[finallen];
	int       littleEndian;
	uint32_t  N;

	if (argc != 4) {
		printf("ERROR: missing argument. Usage: scrypt <hex salt> <hex password> <CPU/memory cost parameter>\n");
		return -1;
	}

	// Check and decode the salt
	binsaltlen = hex2bin(argv[1], &binsalt);

	if ( binsaltlen==0 ) {
		printf("ERROR: Not a valid HEX input value for salt.\n");
		return -2;
	}

	// Check and decode the pass
	binpasslen = hex2bin(argv[2], &binpass);

	if ( binpasslen==0 ) {
		printf("ERROR: Not a valid HEX input value for password.\n");
		return -2;
	}

	if ( only_digits(argv[3])==0 ) {
		printf("ERROR: The CPU/memory cost parameter must be a natural number (use 32768).\n");
		return -2;
	}

	N = atoll(argv[3]);
	if ((N < 2) || ((N & (N - 1)) != 0)) {
		// N = 2^x
		printf("ERROR: The CPU/memory cost parameter (N) must be a power of 2.\n");
		return -2;
	}

	if (N > SCRYPT_SIZE_MAX / 128 / r) {
		printf("ERROR: The CPU/memory cost parameter (N) is too big.\n");
		return -2;
	}

	littleEndian = isLittleEndian();

	if ( littleEndian==1 ) {
		scrypt_reverse(binpass, binpasslen);
		scrypt_reverse(binsalt, binsaltlen);
	}

	// Step1
	retval = scrypt_pbkdf2_sha512(binpass, binpasslen, binsalt, binsaltlen, out, outlen);
	if ( retval!=1 ) {
		printf("ERROR: Failed to execute PKCS5_PBKDF2_HMAC (part 1). Do we have enough memory?\n");
		return -3;
	}

	// Step2
	retval = scrypt_romix(out, outlen, N);
	if ( retval!=0 ) {
		printf("ERROR: Failed to execute ROMix. Do we have enough memory?\n");
		return -3;
	}

	// Step3
	retval = scrypt_pbkdf2_sha512(binpass, binpasslen, out, outlen, final, finallen);
	if ( retval!=1 ) {
		printf("ERROR: Failed to execute PKCS5_PBKDF2_HMAC (part 2). Do we have enough memory?\n");
		return -3;
	}

	if ( littleEndian==1 ) {
		scrypt_reverse(final, finallen);
	}

	hex = bin2hex(final, finallen);

	printf("%s\n", hex);

	secure_wipe((uint8_t *)binpass, sizeof(uint32_t)*binpasslen);
	secure_wipe((uint8_t *)binsalt, sizeof(uint32_t)*binsaltlen);
	secure_wipe((uint8_t *)out,     sizeof(uint32_t)*outlen);
	secure_wipe((uint8_t *)final,   sizeof(uint32_t)*finallen);
	secure_wipe((uint8_t *)hex,     sizeof(uint32_t)*finallen*2); // Each byte is 2 chars long

	free(binpass);
	free(binsalt);
	free(hex);

	return 0;
}
