/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2018 Aaron M. D. Jones <aaronmdjones@gmail.com>
 *
 * OpenSSL frontend data structures for the digest interface.
 */

#include "sysconf.h"

#ifndef ATHEME_INC_DIGEST_FE_HEADER_H
#define ATHEME_INC_DIGEST_FE_HEADER_H 1

#ifndef ATHEME_INC_DIGEST_H
#  error "You should not include me directly; include digest.h instead"
#endif /* !ATHEME_INC_DIGEST_H */

#include <stdbool.h>

#include <openssl/evp.h>

struct digest_context
{
	const EVP_MD *          md;
	void *                  ictx;
	bool                    hmac;
};

#endif /* !ATHEME_INC_DIGEST_FE_HEADER_H */
