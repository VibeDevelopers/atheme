/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2006-2015 Atheme Project (http://atheme.org/)
 * Copyright (C) 2017-2018 Atheme Development Group (https://atheme.github.io/)
 *
 * AUTHCOOKIE mechanism provider
 */

#include "atheme.h"

static const struct sasl_core_functions *sasl_core_functions = NULL;

static unsigned int ATHEME_FATTR_WUR
mech_step(struct sasl_session *const restrict p, const struct sasl_input_buf *const restrict in,
          struct sasl_output_buf ATHEME_VATTR_UNUSED *const restrict out)
{
	if (! (p && in && in->buf && in->len))
		return ASASL_ERROR;

	// This buffer contains sensitive information
	*(in->flags) |= ASASL_INFLAG_WIPE_BUF;

	// Data format: authzid 0x00 authcid 0x00 authcookie [0x00]
	if (in->len > (NICKLEN + 1 + NICKLEN + 1 + AUTHCOOKIE_LENGTH + 1))
		return ASASL_ERROR;

	const char *ptr = in->buf;
	const char *const end = ptr + in->len;

	const char *const authzid = ptr;
	if (! *authzid)
		return ASASL_ERROR;
	if (strlen(authzid) > NICKLEN)
		return ASASL_ERROR;
	if ((ptr += strlen(authzid) + 1) >= end)
		return ASASL_ERROR;

	const char *const authcid = ptr;
	if (! *authcid)
		return ASASL_ERROR;
	if (strlen(authcid) > NICKLEN)
		return ASASL_ERROR;
	if ((ptr += strlen(authcid) + 1) >= end)
		return ASASL_ERROR;

	const char *const secret = ptr;
	if (! *secret)
		return ASASL_ERROR;
	if (strlen(secret) > AUTHCOOKIE_LENGTH)
		return ASASL_ERROR;

	if (! sasl_core_functions->authzid_can_login(p, authzid, NULL))
		return ASASL_ERROR;

	struct myuser *mu = NULL;
	if (! sasl_core_functions->authcid_can_login(p, authcid, &mu))
		return ASASL_ERROR;

	if (! authcookie_find(secret, mu))
		return ASASL_FAIL;

	return ASASL_DONE;
}

static const struct sasl_mechanism mech = {

	.name           = "AUTHCOOKIE",
	.mech_start     = NULL,
	.mech_step      = &mech_step,
	.mech_finish    = NULL,
};

static void
mod_init(struct module *const restrict m)
{
	MODULE_TRY_REQUEST_SYMBOL(m, sasl_core_functions, "saslserv/main", "sasl_core_functions");

	(void) sasl_core_functions->mech_register(&mech);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	(void) sasl_core_functions->mech_unregister(&mech);
}

SIMPLE_DECLARE_MODULE_V1("saslserv/authcookie", MODULE_UNLOAD_CAPABILITY_OK)
