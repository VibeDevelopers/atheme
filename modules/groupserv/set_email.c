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
gs_cmd_set_email(struct sourceinfo *si, int parc, char *parv[])
{
	struct mygroup *mg;
	char *mail = parv[1];

	if (!(mg = gcsyms->mygroup_find(parv[0])))
	{
		command_fail(si, fault_nosuch_target, _("Group \2%s\2 does not exist."), parv[0]);
		return;
	}

	if (! gcsyms->groupacs_sourceinfo_has_flag(mg, si, GA_SET))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to execute this command."));
		return;
	}

	if (!mail || !strcasecmp(mail, "NONE") || !strcasecmp(mail, "OFF"))
	{
		if (metadata_find(mg, "email"))
		{
			metadata_delete(mg, "email");
			command_success_nodata(si, _("The e-mail address for group \2%s\2 was deleted."), entity(mg)->name);
			logcommand(si, CMDLOG_SET, "SET:EMAIL:NONE: \2%s\2", entity(mg)->name);
			return;
		}

		command_fail(si, fault_nochange, _("The e-mail address for group \2%s\2 was not set."), entity(mg)->name);
		return;
	}

	if (!validemail(mail))
	{
		command_fail(si, fault_badparams, _("\2%s\2 is not a valid e-mail address."), mail);
		return;
	}

	// we'll overwrite any existing metadata
	metadata_add(mg, "email", mail);

	logcommand(si, CMDLOG_SET, "SET:EMAIL: \2%s\2 \2%s\2", entity(mg)->name, mail);
	command_success_nodata(si, _("The e-mail address for group \2%s\2 has been set to \2%s\2."), parv[0], mail);
}

static struct command gs_set_email = {
	.name           = "EMAIL",
	.desc           = N_("Sets the group e-mail address."),
	.access         = AC_AUTHENTICATED,
	.maxparc        = 2,
	.cmd            = &gs_cmd_set_email,
	.help           = { .path = "groupserv/set_email" },
};

static void
mod_init(struct module *const restrict m)
{
	MODULE_TRY_REQUEST_SYMBOL(m, gcsyms, "groupserv/main", "groupserv_core_symbols");
	MODULE_TRY_REQUEST_SYMBOL(m, gs_set_cmdtree, "groupserv/set", "gs_set_cmdtree");

	command_add(&gs_set_email, *gs_set_cmdtree);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	command_delete(&gs_set_email, *gs_set_cmdtree);
}

SIMPLE_DECLARE_MODULE_V1("groupserv/set_email", MODULE_UNLOAD_CAPABILITY_OK)
