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

#ifndef _HEX_H_
#define _HEX_H_

#ifdef __cplusplus
extern "C" {
#endif

char *bin2hex(const uint32_t *bin, size_t len);
size_t hex2bin(const char *hex, uint32_t **out);

#ifdef __cplusplus
}
#endif

#endif //_HEX_H_
