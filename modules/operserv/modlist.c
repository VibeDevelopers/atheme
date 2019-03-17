/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2005-2006 William Pitcock, et al.
 *
 * Module listing.
 */

#include <atheme.h>

static void
os_cmd_modlist(struct sourceinfo *si, int parc, char *parv[])
{
	mowgli_node_t *n;
	unsigned int i = 0;
	command_success_nodata(si, _("Loaded modules:"));

	MOWGLI_ITER_FOREACH(n, modules.head)
	{
		struct module *m = n->data;

		if (has_priv(si, PRIV_SERVER_AUSPEX))
		{
			command_success_nodata(si, _("%2d: %-20s [loaded at 0x%lx]"),
				++i, m->name, (unsigned long)m->address);
		}
		else
		{
			command_success_nodata(si, _("%2d: %-20s"), ++i, m->name);
		}
	}

	command_success_nodata(si, _("\2%d\2 modules loaded."), i);
	logcommand(si, CMDLOG_GET, "MODLIST");
}

static struct command os_modlist = {
	.name           = "MODLIST",
	.desc           = N_("Lists loaded modules."),
	.access         = AC_NONE,
	.maxparc        = 0,
	.cmd            = &os_cmd_modlist,
	.help           = { .path = "oservice/modlist" },
};

static void
mod_init(struct module ATHEME_VATTR_UNUSED *const restrict m)
{
	service_named_bind_command("operserv", &os_modlist);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	service_named_unbind_command("operserv", &os_modlist);
}

SIMPLE_DECLARE_MODULE_V1("operserv/modlist", MODULE_UNLOAD_CAPABILITY_OK)
