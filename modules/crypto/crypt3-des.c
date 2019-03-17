/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2018 Atheme Development Group (https://atheme.github.io/)
 *
 * POSIX-style DES crypt(3) wrapper (verify-only).
 */

#include <atheme.h>
#include "crypt3.h"

#ifdef HAVE_CRYPT

static bool ATHEME_FATTR_WUR
atheme_crypt3_des_selftest(void)
{
	static const char password[] = CRYPT3_MODULE_TEST_PASSWORD;
	static const char parameters[] = CRYPT3_MODULE_TEST_VECTOR_DES;

	const char *const result = atheme_crypt3_wrapper(password, parameters, MOWGLI_FUNC_NAME);

	if (! result)
		return false;

	if (strcmp(result, parameters) != 0)
	{
		(void) slog(LG_ERROR, "%s: crypt(3) returned an incorrect result", MOWGLI_FUNC_NAME);
		(void) slog(LG_ERROR, "%s: expected '%s', got '%s'", MOWGLI_FUNC_NAME, parameters, result);
		return false;
	}

	return true;
}

static bool ATHEME_FATTR_WUR
atheme_crypt3_des_verify(const char *const restrict password, const char *const restrict parameters,
                         unsigned int ATHEME_VATTR_UNUSED *const restrict flags)
{
	char hash[BUFSIZE];

	if (strlen(parameters) != CRYPT3_LOADHASH_LENGTH_DES)
	{
		(void) slog(LG_DEBUG, "%s: params not %u characters long", MOWGLI_FUNC_NAME,
		                      CRYPT3_LOADHASH_LENGTH_DES);
		return false;
	}
	if (sscanf(parameters, CRYPT3_LOADHASH_FORMAT_DES, hash) != 1)
	{
		(void) slog(LG_DEBUG, "%s: sscanf(3) was unsuccessful", MOWGLI_FUNC_NAME);
		return false;
	}
	if (strcmp(parameters, hash) != 0)
	{
		(void) slog(LG_DEBUG, "%s: strcmp(3) mismatch", MOWGLI_FUNC_NAME);
		return false;
	}

	/* The above strlen(3)/sscanf(3)/strcmp(3) are only cautionary, to avoid wasting some time passing data
	 * to crypt(3) that it obviously did not generate.  However, the DES hash format is not at all unique
	 * or reliably identifiable, and so we cannot be sure that even though we got to this point, that the
	 * digest was indeed produced by DES crypt(3).  For this reason, we DON'T set PWVERIFY_FLAG_MYMODULE.
	 */

	(void) slog(LG_DEBUG, CRYPT3_MODULE_WARNING, MOWGLI_FUNC_NAME);

	const char *const result = atheme_crypt3_wrapper(password, parameters, MOWGLI_FUNC_NAME);

	if (! result)
		return false;

	if (strcmp(parameters, result) != 0)
	{
		(void) slog(LG_DEBUG, "%s: strcmp(3) mismatch, invalid password?", MOWGLI_FUNC_NAME);
		(void) slog(LG_DEBUG, "%s: expected '%s', got '%s'", MOWGLI_FUNC_NAME, parameters, result);
		return false;
	}

	return true;
}

static const struct crypt_impl crypto_crypt3_impl = {

	.id        = "crypt3-des",
	.verify    = &atheme_crypt3_des_verify,
};

static void
mod_init(struct module *const restrict m)
{
	if (! atheme_crypt3_des_selftest())
	{
		(void) slog(LG_ERROR, "%s: self-test failed, does this platform support this algorithm?", m->name);

		m->mflags |= MODFLAG_FAIL;
		return;
	}

	(void) slog(LG_INFO, CRYPT3_MODULE_WARNING, m->name);

	(void) crypt_register(&crypto_crypt3_impl);

	m->mflags |= MODFLAG_DBCRYPTO;
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	(void) crypt_unregister(&crypto_crypt3_impl);
}

#else /* HAVE_CRYPT */

static void
mod_init(struct module *const restrict m)
{
	(void) slog(LG_ERROR, "Module %s requires crypt(3) support, refusing to load.", m->name);

	m->mflags |= MODFLAG_FAIL;
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	// Nothing To Do
}

#endif /* !HAVE_CRYPT */

SIMPLE_DECLARE_MODULE_V1("crypto/crypt3-des", MODULE_UNLOAD_CAPABILITY_OK)
