/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2005-2010 Atheme Project (http://atheme.org/)
 *
 * This file contains routines to handle the GroupServ HELP command.
 */

#include "atheme.h"
#include "groupserv.h"

static const struct groupserv_core_symbols *gcsyms = NULL;
static mowgli_patricia_t **gs_set_cmdtree = NULL;

static void
gs_cmd_set_open(struct sourceinfo *si, int parc, char *parv[])
{
	struct mygroup *mg;

	if (!parv[0] || !parv[1])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "OPEN");
		command_fail(si, fault_needmoreparams, _("Syntax: OPEN <!group> <ON|OFF>"));
		return;
	}

	if ((mg = gcsyms->mygroup_find(parv[0])) == NULL)
	{
		command_fail(si, fault_nosuch_target, _("The group \2%s\2 does not exist."), parv[0]);
		return;
	}

	if (! gcsyms->groupacs_sourceinfo_has_flag(mg, si, GA_FOUNDER))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to execute this command."));
		return;
	}

	if (!strcasecmp(parv[1], "ON"))
	{
		if (!gcsyms->config->enable_open_groups)
		{
			command_fail(si, fault_nochange, _("Setting groups as open has been administratively disabled."));
			return;
		}

		if (mg->flags & MG_OPEN)
		{
			command_fail(si, fault_nochange, _("\2%s\2 is already open to anyone joining."), entity(mg)->name);
			return;
		}

		mg->flags |= MG_OPEN;

		logcommand(si, CMDLOG_SET, "OPEN:ON: \2%s\2", entity(mg)->name);
		command_success_nodata(si, _("\2%s\2 is now open to anyone joining."), entity(mg)->name);
	}
	else if (!strcasecmp(parv[1], "OFF"))
	{
		if (!(mg->flags & MG_OPEN))
		{
			command_fail(si, fault_nochange, _("\2%s\2 is already not open to anyone joining."), entity(mg)->name);
			return;
		}

		mg->flags &= ~MG_OPEN;

		logcommand(si, CMDLOG_SET, "OPEN:OFF: \2%s\2", entity(mg)->name);
		command_success_nodata(si, _("\2%s\2 is no longer open to anyone joining."), entity(mg)->name);
	}
	else
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "OPEN");
		command_fail(si, fault_badparams, _("Syntax: OPEN <!group> <ON|OFF>"));
	}
}

static struct command gs_set_open = {
	.name           = "OPEN",
	.desc           = N_("Sets the group as open for anyone to join."),
	.access         = AC_AUTHENTICATED,
	.maxparc        = 2,
	.cmd            = &gs_cmd_set_open,
	.help           = { .path = "groupserv/set_open" },
};

static void
mod_init(struct module *const restrict m)
{
	MODULE_TRY_REQUEST_SYMBOL(m, gcsyms, "groupserv/main", "groupserv_core_symbols");
	MODULE_TRY_REQUEST_SYMBOL(m, gs_set_cmdtree, "groupserv/set", "gs_set_cmdtree");

	command_add(&gs_set_open, *gs_set_cmdtree);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	command_delete(&gs_set_open, *gs_set_cmdtree);
}

SIMPLE_DECLARE_MODULE_V1("groupserv/set_open", MODULE_UNLOAD_CAPABILITY_OK)
