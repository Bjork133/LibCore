//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An implementation of the scrypt KDF that decouples the PBKDF2 from the rest of scrypt, in order
// to be able to handle different hash functions (SHA512, SHA3 etc).
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
 * and transformed to work with C, with additional inspiration from:
 *
 * https://github.com/Tarsnap/scrypt/blob/master/lib/crypto/crypto_scrypt-ref.c
 * https://github.com/technion/libscrypt/blob/master/crypto_scrypt-nosse.c
 */

/**
 * Copyright (c) 2013, Joshua Small
 *  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright 2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */

#include <stdlib.h>
#include <stdint.h>
#include "util.h"

// Converts Big <-> Little Endian words
void scrypt_reverse(uint32_t * words, size_t len)
{
	size_t i;
	uint32_t out;

	for (i = 0; i < len; i++) {
		out = words[i] &  0xFF;
		out = (out << 8) | ((words[i] >>  8) & 0xFF);
		out = (out << 8) | ((words[i] >> 16) & 0xFF);
		out = (out << 8) | ((words[i] >> 24) & 0xFF);

		words[i] = out;
	}
}

void scrypt_blkcpy(const uint32_t * S, size_t Si, uint32_t * D, size_t Di, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		D[Di + i] = S[Si + i];
}

void scrypt_blkxor(const uint32_t * S, size_t Si, uint32_t * D, size_t Di, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		D[Di + i] = D[Di + i] ^ S[Si + i];
}

void scrypt_salsa20(uint32_t in[16], int rounds)
{
	int i;
	uint32_t x[16];

	for (i = 0;i < 16;++i) x[i] = in[i];

	for (i = rounds;i > 0;i -= 2) {

#define R(a,b) (((a) << (b)) | ((a) >> (32 - (b))))

		x[ 4] ^= R(x[ 0]+x[12], 7);  x[ 8] ^= R(x[ 4]+x[ 0], 9);
		x[12] ^= R(x[ 8]+x[ 4],13);  x[ 0] ^= R(x[12]+x[ 8],18);
		x[ 9] ^= R(x[ 5]+x[ 1], 7);  x[13] ^= R(x[ 9]+x[ 5], 9);
		x[ 1] ^= R(x[13]+x[ 9],13);  x[ 5] ^= R(x[ 1]+x[13],18);
		x[14] ^= R(x[10]+x[ 6], 7);  x[ 2] ^= R(x[14]+x[10], 9);
		x[ 6] ^= R(x[ 2]+x[14],13);  x[10] ^= R(x[ 6]+x[ 2],18);
		x[ 3] ^= R(x[15]+x[11], 7);  x[ 7] ^= R(x[ 3]+x[15], 9);
		x[11] ^= R(x[ 7]+x[ 3],13);  x[15] ^= R(x[11]+x[ 7],18);
		x[ 1] ^= R(x[ 0]+x[ 3], 7);  x[ 2] ^= R(x[ 1]+x[ 0], 9);
		x[ 3] ^= R(x[ 2]+x[ 1],13);  x[ 0] ^= R(x[ 3]+x[ 2],18);
		x[ 6] ^= R(x[ 5]+x[ 4], 7);  x[ 7] ^= R(x[ 6]+x[ 5], 9);
		x[ 4] ^= R(x[ 7]+x[ 6],13);  x[ 5] ^= R(x[ 4]+x[ 7],18);
		x[11] ^= R(x[10]+x[ 9], 7);  x[ 8] ^= R(x[11]+x[10], 9);
		x[ 9] ^= R(x[ 8]+x[11],13);  x[10] ^= R(x[ 9]+x[ 8],18);
		x[12] ^= R(x[15]+x[14], 7);  x[13] ^= R(x[12]+x[15], 9);
		x[14] ^= R(x[13]+x[12],13);  x[15] ^= R(x[14]+x[13],18);

#undef R

	}

	for (i = 0;i < 16;++i) in[i] = x[i] + in[i];

	// Secure wipe
	secure_wipe((uint8_t *)x, sizeof(uint32_t)*16);
}

void scrypt_blockmix(uint32_t * block, size_t length)
{
	uint32_t X[16];
	uint32_t out[length];
	size_t len = length / 16;
	size_t i;

	scrypt_blkcpy(block, length - 16, X, 0, 16);

	for (i = 0; i < len; i++) {
		scrypt_blkxor(block, 16 * i, X, 0, 16);
		scrypt_salsa20(X, 8);

		if ((i & 1) == 0) {
			scrypt_blkcpy(X, 0, out, 8 * i, 16);
		} else {
			scrypt_blkcpy(X, 0, out, 8 * (i^1 + len), 16);
		}
	}

	scrypt_blkcpy(out, 0, block, 0, length);

	// Secure wipe
	secure_wipe((uint8_t *)X,   sizeof(uint32_t)*16);
	secure_wipe((uint8_t *)out, sizeof(uint32_t)*length);
}

int scrypt_romix(uint32_t * block, size_t len, int N)
{
	uint32_t  X[len];
	uint32_t *V;
	size_t    i;
	size_t    j;

	V = malloc(sizeof(uint32_t)*len*N);
	if ( V==NULL ) {
		return -1;
	}

	scrypt_blkcpy(block, 0, X, 0, len);

	for (i = 0; i < N; i++) {
		scrypt_blkcpy(X, 0, V, i*len, len);
		scrypt_blockmix(X, len);
	}

	for (i = 0; i < N; i++) {
		j = X[len - 16] & (N - 1);

		scrypt_blkxor(V, j*len, X, 0, len);
		scrypt_blockmix(X, len);
	}

	scrypt_blkcpy(X, 0, block, 0, len);

	// Secure wipe
	secure_wipe((uint8_t *)X, sizeof(uint32_t)*len);
	secure_wipe((uint8_t *)V, sizeof(uint32_t)*len*N);

	free(V);

	return 0;
}
