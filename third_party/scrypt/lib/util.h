//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Utility function to check if an input string consists of only digits.
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

#ifndef _SCRYPTUTIL_H_
#define _SCRYPTUTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

int only_digits(const char *s);
int isLittleEndian();
void secure_wipe(uint8_t* v, uint32_t n);
#ifdef __cplusplus
}
#endif

#endif //_SCRYPTUTIL_H_
