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

#ifndef _SCRYPTLIB_H_
#define _SCRYPTLIB_H_

#ifdef __cplusplus
extern "C" {
#endif

void scrypt_reverse(uint32_t * words, size_t len);
void scrypt_blkcpy(const uint32_t * S, size_t Si, uint32_t * D, size_t Di, size_t len);
void scrypt_blkxor(const uint32_t * S, size_t Si, uint32_t * D, size_t Di, size_t len);
void scrypt_salsa20(uint32_t in[16], int rounds);
void scrypt_blockmix(uint32_t * block, size_t length);
int  scrypt_romix(uint32_t * block, size_t len, int N);

#ifdef __cplusplus
}
#endif

#endif //_SCRYPTLIB_H_
