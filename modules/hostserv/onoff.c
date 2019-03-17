/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2005-2009 William Pitcock <nenolod -at- nenolod.net>
 *
 * Allows setting a vhost on/off
 */

#include <atheme.h>
#include "hostserv.h"

static void
hs_cmd_on(struct sourceinfo *si, int parc, char *parv[])
{
	struct mynick *mn = NULL;
	struct metadata *md;
	char buf[BUFSIZE];

	if (si->su == NULL)
	{
		command_fail(si, fault_noprivs, _("\2%s\2 can only be executed via IRC."), "ON");
		return;
	}

	mn = mynick_find(si->su->nick);
	if (mn == NULL)
	{
		command_fail(si, fault_nosuch_target, _("Nick \2%s\2 is not registered."), si->su->nick);
		return;
	}
	if (mn->owner != si->smu && !myuser_access_verify(si->su, mn->owner))
	{
		command_fail(si, fault_noprivs, _("You are not recognized as \2%s\2."), entity(mn->owner)->name);
		return;
	}
	snprintf(buf, BUFSIZE, "%s:%s", "private:usercloak", mn->nick);
	md = metadata_find(si->smu, buf);
	if (md == NULL)
		md = metadata_find(si->smu, "private:usercloak");
	if (md == NULL)
	{
		command_success_nodata(si, _("Please contact an Operator to get a vhost assigned to this nick."));
		return;
	}
	do_sethost(si->su, md->value);
	command_success_nodata(si, _("Your vhost of \2%s\2 is now activated."), md->value);
}

static void
hs_cmd_off(struct sourceinfo *si, int parc, char *parv[])
{
	struct mynick *mn = NULL;
	struct metadata *md;
	char buf[BUFSIZE];

	if (si->su == NULL)
	{
		command_fail(si, fault_noprivs, _("\2%s\2 can only be executed via IRC."), "OFF");
		return;
	}

	mn = mynick_find(si->su->nick);
	if (mn == NULL)
	{
		command_fail(si, fault_nosuch_target, _("Nick \2%s\2 is not registered."), si->su->nick);
		return;
	}
	if (mn->owner != si->smu && !myuser_access_verify(si->su, mn->owner))
	{
		command_fail(si, fault_noprivs, _("You are not recognized as \2%s\2."), entity(mn->owner)->name);
		return;
	}
	snprintf(buf, BUFSIZE, "%s:%s", "private:usercloak", mn->nick);
	md = metadata_find(si->smu, buf);
	if (md == NULL)
		md = metadata_find(si->smu, "private:usercloak");
	if (md == NULL)
	{
		command_success_nodata(si, _("Please contact an Operator to get a vhost assigned to this nick."));
		return;
	}
	do_sethost(si->su, NULL);
	command_success_nodata(si, _("Your vhost of \2%s\2 is now deactivated."), md->value);
}

static struct command hs_on = {
	.name           = "ON",
	.desc           = N_("Activates your assigned vhost."),
	.access         = AC_AUTHENTICATED,
	.maxparc        = 1,
	.cmd            = &hs_cmd_on,
	.help           = { .path = "hostserv/on" },
};

static struct command hs_off = {
	.name           = "OFF",
	.desc           = N_("Deactivates your assigned vhost."),
	.access         = AC_AUTHENTICATED,
	.maxparc        = 1,
	.cmd            = &hs_cmd_off,
	.help           = { .path = "hostserv/off" },
};

static void
mod_init(struct module ATHEME_VATTR_UNUSED *const restrict m)
{
	service_named_bind_command("hostserv", &hs_on);
	service_named_bind_command("hostserv", &hs_off);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	service_named_unbind_command("hostserv", &hs_on);
	service_named_unbind_command("hostserv", &hs_off);
}

SIMPLE_DECLARE_MODULE_V1("hostserv/onoff", MODULE_UNLOAD_CAPABILITY_OK)
