/*
 * Copyright (c) 1999-2006 Andrija Antonijevic, Stefan Burstroem.
 * Copyright (c) 2014-2022 AmiSSL Open Source Team.
 * All Rights Reserved.
 *
 * This file has been modified for use with AmiSSL for AmigaOS-based systems.
 *
 * Copyright 1995-2020 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#if !defined(PROTO_AMISSL_H) && !defined(AMISSL_COMPILE)
# include <proto/amissl.h>
#endif

#ifndef OPENSSL_RC4_H
# define OPENSSL_RC4_H
# if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
#  pragma once
# endif

# include <openssl/macros.h>
# ifndef OPENSSL_NO_DEPRECATED_3_0
#  define HEADER_RC4_H
# endif

# include <openssl/opensslconf.h>

# ifndef OPENSSL_NO_RC4
#  include <stddef.h>
#  ifdef  __cplusplus
extern "C" {
#  endif

#  ifndef OPENSSL_NO_DEPRECATED_3_0
typedef struct rc4_key_st {
    RC4_INT x, y;
    RC4_INT data[256];
} RC4_KEY;
#  endif
#  ifndef OPENSSL_NO_DEPRECATED_3_0
OSSL_DEPRECATEDIN_3_0 const char *RC4_options(void);
OSSL_DEPRECATEDIN_3_0 void RC4_set_key(RC4_KEY *key, int len,
                                       const unsigned char *data);
OSSL_DEPRECATEDIN_3_0 void RC4(RC4_KEY *key, size_t len,
                               const unsigned char *indata,
                               unsigned char *outdata);
#  endif

#  ifdef  __cplusplus
}
#  endif
# endif

#endif
