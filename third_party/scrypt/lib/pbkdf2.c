//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An implementation of the PBKDF2 using SHA512.
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
 * and transformed to work with C and reduced functionality for scrypt
 */

#include <stdlib.h>
#include <stdint.h>
#include "hmac_sha2.h"
#include "util.h"
#include "scrypt_lib.h"

int scrypt_pbkdf2_sha512(const uint32_t *pass, size_t passlen,
		const uint32_t *salt, size_t saltlen, uint32_t *out, size_t outlen) {
	size_t   k;
	uint32_t saltnew[saltlen + 1];
	uint32_t mac[SHA512_DIGEST_SIZE/4];
	int      littleEndian;

	if (outlen < 0 ) {
		return -1;
	}

	littleEndian = isLittleEndian();

	scrypt_blkcpy(salt, 0, saltnew, 0, saltlen);

	for (k = 0; k * 16<outlen; k++) {
		saltnew[saltlen] = k + 1;
		// Funny thing is we need to reverse this number :-)
		if ( littleEndian==1 ) {
			scrypt_reverse(saltnew + saltlen, 1);
		}

		hmac_sha512((unsigned char *)pass, passlen*4,
			(unsigned char *)saltnew, (saltlen+1)*4,
			(unsigned char *)mac, SHA512_DIGEST_SIZE);

		scrypt_blkcpy(mac, 0, out, k * 16, SHA512_DIGEST_SIZE/4);
	}

	secure_wipe((uint8_t *)saltnew, sizeof(uint32_t)*(saltlen + 1));
	secure_wipe((uint8_t *)mac,     sizeof(uint32_t)*(SHA512_DIGEST_SIZE/4));

	return 1;
}
