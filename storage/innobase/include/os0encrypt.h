/*****************************************************************************

Copyright (c) 1995, 2019, Alibaba and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is also distributed with certain software (including
but not limited to OpenSSL) that is licensed under separate terms,
as designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have included with MySQL.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

#ifndef os_encrypt_h
#define os_encrypt_h

#include "os0file.h"
#include <functional>

/** Data encrypt algorithm */
enum data_encrypt_algorithm { SM4_128_CTR, AES_256_CBC };
extern ulong encrypt_algorithm;

/** Get encrypt algorithm
@return algorithm type */
extern Encryption::Type encrypt_type();

/** Encrtytion interfce */
using Encrypt_func = std::function<int(
    const unsigned char *source, uint32 source_length, unsigned char *dest,
    const unsigned char *key, uint32 key_length, const unsigned char *iv,
    bool padding)>;
/** DEcrtytion interfce */
using Decrypt_func = std::function<int(
    const unsigned char *source, uint32 source_length, unsigned char *dest,
    const unsigned char *key, uint32 key_length, const unsigned char *iv,
    bool padding)>;

#define ENCRYPT_BAD_DATA (-1)

/** Get enctypt function */
extern Encrypt_func get_encrypt_func(Encryption::Type type);

/** Get dectypt function */
extern Decrypt_func get_decrypt_func(Encryption::Type type);

#endif
