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

#include "os0encrypt.h"
#include "my_aes.h"
#include "my_sm4.h"

/** Data encrypt algorithm */
ulong encrypt_algorithm;

/** Get encrypt algorithm
@return algorithm type */
Encryption::Type encrypt_type() {
  return encrypt_algorithm == SM4_128_CTR ? Encryption::SM4 : Encryption::AES;
}

/** Wrap SM4 encrypt function */
static int wrap_sm4_encrypt(const unsigned char *source, uint32 source_length,
                            unsigned char *dest, const unsigned char *key,
                            uint32 key_length, const unsigned char *iv, bool) {
  int ret =
      my_sm4_encrypt(source, source_length, dest, key, key_length, SM4_CTR, iv);
  return (ret < 0) ? ENCRYPT_BAD_DATA : ret;
}

/** Wrap SM4 decrypt function */
static int wrap_sm4_decrypt(const unsigned char *source, uint32 source_length,
                            unsigned char *dest, const unsigned char *key,
                            uint32 key_length, const unsigned char *iv, bool) {
  int ret =
      my_sm4_decrypt(source, source_length, dest, key, key_length, SM4_CTR, iv);
  return (ret < 0) ? ENCRYPT_BAD_DATA : ret;
}

/** Wrap AES encrypt function */
static int wrap_aes_encrypt(const unsigned char *source, uint32 source_length,
                            unsigned char *dest, const unsigned char *key,
                            uint32 key_length, const unsigned char *iv,
                            bool padding) {
  int ret = my_aes_encrypt(source, source_length, dest, key, key_length,
                           my_aes_256_cbc, iv, padding);
  return (ret == MY_AES_BAD_DATA) ? ENCRYPT_BAD_DATA : ret;
}

/** Wrap AES decrypt function */
static int wrap_aes_decrypt(const unsigned char *source, uint32 source_length,
                            unsigned char *dest, const unsigned char *key,
                            uint32 key_length, const unsigned char *iv,
                            bool padding) {
  int ret = my_aes_decrypt(source, source_length, dest, key, key_length,
                           my_aes_256_cbc, iv, padding);
  return (ret == MY_AES_BAD_DATA) ? ENCRYPT_BAD_DATA : ret;
}

/** Get enctypt function */
Encrypt_func get_encrypt_func(Encryption::Type type) {
  Encrypt_func func = NULL;

  switch (type) {
    case Encryption::AES:
      ut_ad(encrypt_algorithm == AES_256_CBC);
      func = wrap_aes_encrypt;
      break;
    case Encryption::SM4:
      ut_ad(encrypt_algorithm == SM4_128_CTR);
      func = wrap_sm4_encrypt;
      break;
    default:
      ut_error;
  }

  return func;
}

/** Get dectypt function */
Decrypt_func get_decrypt_func(Encryption::Type type) {
  Encrypt_func func = NULL;

  switch (type) {
    case Encryption::AES:
      ut_ad(encrypt_algorithm == AES_256_CBC);
      func = wrap_aes_decrypt;
      break;
    case Encryption::SM4:
      ut_ad(encrypt_algorithm == SM4_128_CTR);
      func = wrap_sm4_decrypt;
      break;
    default:
      ut_error;
  }

  return func;
}
