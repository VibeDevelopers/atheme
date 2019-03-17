/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2018 Atheme Development Group (https://atheme.github.io/)
 *
 * POSIX-style MD5 crypt(3) wrapper (verify-only).
 */

#include <atheme.h>
#include "crypt3.h"

#ifdef HAVE_CRYPT

static bool ATHEME_FATTR_WUR
atheme_crypt3_md5_selftest(void)
{
	static const char password[] = CRYPT3_MODULE_TEST_PASSWORD;
	static const char parameters[] = CRYPT3_MODULE_TEST_VECTOR_MD5;

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
atheme_crypt3_md5_verify(const char *const restrict password, const char *const restrict parameters,
                         unsigned int *const restrict flags)
{
	char hash[BUFSIZE];

	if (sscanf(parameters, CRYPT3_LOADHASH_FORMAT_MD5, hash) != 1)
	{
		(void) slog(LG_DEBUG, "%s: sscanf(3) was unsuccessful", MOWGLI_FUNC_NAME);
		return false;
	}
	if (strlen(hash) != CRYPT3_LOADHASH_LENGTH_MD5)
	{
		(void) slog(LG_DEBUG, "%s: digest not %u characters long", MOWGLI_FUNC_NAME,
		                      CRYPT3_LOADHASH_LENGTH_MD5);
		return false;
	}

	*flags |= PWVERIFY_FLAG_MYMODULE;

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

	.id        = "crypt3-md5",
	.verify    = &atheme_crypt3_md5_verify,
};

static void
mod_init(struct module *const restrict m)
{
	if (! atheme_crypt3_md5_selftest())
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

SIMPLE_DECLARE_MODULE_V1("crypto/crypt3-md5", MODULE_UNLOAD_CAPABILITY_OK)
