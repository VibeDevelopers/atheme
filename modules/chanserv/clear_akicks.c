/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2014 Atheme Project (http://atheme.org/)
 *
 * This file contains code for the CService CLEAR FLAGS functions.
 */

#include <atheme.h>

static mowgli_patricia_t **cs_clear_cmds = NULL;

static void
cs_cmd_clear_akicks(struct sourceinfo *si, int parc, char *parv[])
{
	struct mychan *mc;
	mowgli_node_t *n, *tn;
	struct chanacs *ca;
	char *name = parv[0];
	int changes = 0;

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CLEAR AKICKS");
		command_fail(si, fault_needmoreparams, "Syntax: CLEAR <#channel> AKICKS");
		return;
	}

	if (!(mc = mychan_find(name)))
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not registered.", name);
		return;
	}

	if (metadata_find(mc, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, "\2%s\2 is closed.", name);
		return;
	}

	if (!mc->chan)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 does not exist.", name);
		return;
	}

	if (!(chanacs_source_has_flag(mc, si, CA_RECOVER) && chanacs_source_has_flag(mc, si, CA_FLAGS)))
	{
		command_fail(si, fault_noprivs, "You are not authorized to perform this operation.");
		return;
	}


	MOWGLI_ITER_FOREACH_SAFE(n, tn, mc->chanacs.head)
	{
		ca = n->data;

		if (ca->level != CA_AKICK)
			continue;

		changes++;
		atheme_object_unref(ca);
	}

	logcommand(si, CMDLOG_DO, "CLEAR:AKICKS: \2%s\2", mc->name);
	command_success_nodata(si, _("Cleared AKICK entries in \2%s\2."), name);
	if (changes > 0)
		verbose(mc, _("\2%s\2 removed all %d AKICK entries."),
				get_source_name(si), changes);
}

static struct command cs_clear_akicks = {
	.name           = "AKICKS",
	.desc           = N_("Clears all channel AKICK entries."),
	.access         = AC_NONE,
	.maxparc        = 2,
	.cmd            = &cs_cmd_clear_akicks,
	.help           = { .path = "cservice/clear_akicks" },
};

static void
mod_init(struct module *const restrict m)
{
	MODULE_TRY_REQUEST_SYMBOL(m, cs_clear_cmds, "chanserv/clear", "cs_clear_cmds");

        command_add(&cs_clear_akicks, *cs_clear_cmds);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	command_delete(&cs_clear_akicks, *cs_clear_cmds);
}

SIMPLE_DECLARE_MODULE_V1("chanserv/clear_akicks", MODULE_UNLOAD_CAPABILITY_OK)
