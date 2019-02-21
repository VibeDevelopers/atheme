/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2005-2006 Patrick Fish, et al.
 *
 * This file contains code for OS UPTIME
 */

#include "atheme.h"

static void
os_cmd_uptime(struct sourceinfo *si, int parc, char *parv[])
{
	logcommand(si, CMDLOG_GET, "UPTIME");

#ifdef ATHEME_ENABLE_REPRODUCIBLE_BUILDS
	command_success_nodata(si, "%s [%s]", PACKAGE_STRING, revision);
#else /* ATHEME_ENABLE_REPRODUCIBLE_BUILDS */
        command_success_nodata(si, "%s [%s] Build Date: %s", PACKAGE_STRING, revision, __DATE__);
#endif /* !ATHEME_ENABLE_REPRODUCIBLE_BUILDS */

        command_success_nodata(si, _("Services have been up for %s"), timediff(CURRTIME - me.start));
	command_success_nodata(si, _("Current PID: %d"), getpid());
        command_success_nodata(si, _("Registered accounts: %d"), cnt.myuser);

	if (!nicksvs.no_nick_ownership)
        	command_success_nodata(si, _("Registered nicknames: %d"), cnt.mynick);

        command_success_nodata(si, _("Registered channels: %d"), cnt.mychan);
        command_success_nodata(si, _("Users currently online: %d"), cnt.user - me.me->users);
}

static struct command os_uptime = {
	.name           = "UPTIME",
	.desc           = N_("Shows services uptime and the number of registered nicks and channels."),
	.access         = PRIV_SERVER_AUSPEX,
	.maxparc        = 1,
	.cmd            = &os_cmd_uptime,
	.help           = { .path = "oservice/uptime" },
};

static void
mod_init(struct module ATHEME_VATTR_UNUSED *const restrict m)
{
        service_named_bind_command("operserv", &os_uptime);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	service_named_unbind_command("operserv", &os_uptime);
}

SIMPLE_DECLARE_MODULE_V1("operserv/uptime", MODULE_UNLOAD_CAPABILITY_OK)
