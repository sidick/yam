/*
 * Generated by util/mkerr.pl DO NOT EDIT
 * Copyright (c) 1999-2006 Andrija Antonijevic, Stefan Burstroem.
 * Copyright (c) 2014-2022 AmiSSL Open Source Team.
 * All Rights Reserved.
 *
 * This file has been modified for use with AmiSSL for AmigaOS-based systems.
 *
 * Copyright 1995-2021 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#if !defined(PROTO_AMISSL_H) && !defined(AMISSL_COMPILE)
# include <proto/amissl.h>
#endif

#ifndef OPENSSL_PKCS12ERR_H
# define OPENSSL_PKCS12ERR_H
# if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
#  pragma once
# endif

# include <openssl/opensslconf.h>
# include <openssl/symhacks.h>
# include <openssl/cryptoerr_legacy.h>



/*
 * PKCS12 reason codes.
 */
# define PKCS12_R_CANT_PACK_STRUCTURE                     100
# define PKCS12_R_CONTENT_TYPE_NOT_DATA                   121
# define PKCS12_R_DECODE_ERROR                            101
# define PKCS12_R_ENCODE_ERROR                            102
# define PKCS12_R_ENCRYPT_ERROR                           103
# define PKCS12_R_ERROR_SETTING_ENCRYPTED_DATA_TYPE       120
# define PKCS12_R_INVALID_NULL_ARGUMENT                   104
# define PKCS12_R_INVALID_NULL_PKCS12_POINTER             105
# define PKCS12_R_INVALID_TYPE                            112
# define PKCS12_R_IV_GEN_ERROR                            106
# define PKCS12_R_KEY_GEN_ERROR                           107
# define PKCS12_R_MAC_ABSENT                              108
# define PKCS12_R_MAC_GENERATION_ERROR                    109
# define PKCS12_R_MAC_SETUP_ERROR                         110
# define PKCS12_R_MAC_STRING_SET_ERROR                    111
# define PKCS12_R_MAC_VERIFY_FAILURE                      113
# define PKCS12_R_PARSE_ERROR                             114
# define PKCS12_R_PKCS12_CIPHERFINAL_ERROR                116
# define PKCS12_R_UNKNOWN_DIGEST_ALGORITHM                118
# define PKCS12_R_UNSUPPORTED_PKCS12_MODE                 119

#endif
