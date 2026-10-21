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

#ifndef _PBKDF2_H_
#define _PBKDF2_H_

#ifdef __cplusplus
extern "C" {
#endif

int scrypt_pbkdf2_sha512(const uint32_t *pass, size_t passlen,
		const uint32_t *salt, size_t saltlen, uint32_t *out, size_t outlen);

#ifdef __cplusplus
}
#endif

#endif //_PBKDF2_H_
