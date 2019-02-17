/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2017 Aaron M. D. Jones <aaronmdjones@gmail.com>
 */

#include "stdheaders.h"

#ifndef ATHEME_INC_BASE64_H
#define ATHEME_INC_BASE64_H 1

#include "attrs.h"

#define BASE64_SIZE(len)        ((((len) * 4) / 3) + 4)

size_t base64_encode(const void *src, size_t srclength, char *target, size_t targsize) ATHEME_FATTR_WUR;
size_t base64_encode_raw(const void *src, size_t srclength, char *target, size_t targsize) ATHEME_FATTR_WUR;
size_t base64_decode(const char *src, void *target, size_t targsize) ATHEME_FATTR_WUR;

#endif /* !ATHEME_INC_BASE64_H */
