/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2009 Michael Rodriguez <dkingston@dkingston.net>
 *
 * Reloads a module.
 */

#include <atheme.h>

static void
recurse_module_deplist(struct module *m, mowgli_list_t *deplist)
{
	mowgli_node_t *n;
	MOWGLI_LIST_FOREACH(n, m->dephost.head)
	{
		struct module *dm = (struct module *) n->data;

		// Skip duplicates
		bool found = false;
		mowgli_node_t *n2;
		MOWGLI_LIST_FOREACH(n2, deplist->head)
		{
			struct module_dependency *existing_dep = n2->data;
			if (0 == strcasecmp(dm->name, existing_dep->name))
				found = true;
		}
		if (found)
			continue;

		struct module_dependency *const dep = smalloc(sizeof *dep);
		dep->name = sstrdup(dm->name);
		dep->can_unload = dm->can_unload;
		mowgli_node_add(dep, mowgli_node_create(), deplist);

		recurse_module_deplist(dm, deplist);
	}
}

static void
os_cmd_modreload(struct sourceinfo *si, int parc, char *parv[])
{
	char *module = parv[0];
	struct module *m;
	mowgli_node_t *n;
	struct module_dependency * reloading_semipermanent_module = NULL;

	if (parc < 1)
        {
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "MODRELOAD");
		command_fail(si, fault_needmoreparams, _("Syntax: MODRELOAD <module...>"));
		return;
        }

	if (!(m = module_find_published(module)))
	{
		command_fail(si, fault_nochange, _("\2%s\2 is not loaded."), module);
		return;
	}

	/* Make sure these checks happen before we construct the list of dependent modules, so that
	 * we can return without having to clean up everything.
	 */

	if (!strcmp(m->name, "operserv/main") || !strcmp(m->name, "operserv/modload") || !strcmp(m->name, "operserv/modunload") || !strcmp(m->name, "operserv/modreload"))
	{
		command_fail(si, fault_noprivs, _("Refusing to reload \2%s\2."), module);
		return;
	}

	if (m->can_unload == MODULE_UNLOAD_CAPABILITY_NEVER)
	{
		command_fail(si, fault_noprivs, _("\2%s\2 is a permanent module; it cannot be reloaded."), module);
		slog(LG_ERROR, "MODRELOAD:ERROR: \2%s\2 tried to reload permanent module \2%s\2", get_oper_name(si), module);
		return;
	}

	mowgli_list_t *module_deplist = mowgli_list_create();
	struct module_dependency *const self_dep = smalloc(sizeof *self_dep);
	self_dep->name = sstrdup(module);
	self_dep->can_unload = m->can_unload;
	mowgli_node_add(self_dep, mowgli_node_create(), module_deplist);
	recurse_module_deplist(m, module_deplist);

	MOWGLI_LIST_FOREACH(n, module_deplist->head)
	{
		struct module_dependency *dep = n->data;
		if (dep->can_unload == MODULE_UNLOAD_CAPABILITY_NEVER)
		{
			command_fail(si, fault_noprivs, _("\2%s\2 is depended upon by \2%s\2, which is a permanent module and cannot be reloaded."), module, dep->name);
			slog(LG_ERROR, "MODRELOAD:ERROR: \2%s\2 tried to reload \2%s\2, which is depended upon by permanent module \2%s\2", get_oper_name(si), module, dep->name);

			// We've constructed the dep list now, so don't return without cleaning up
			while (module_deplist->head != NULL)
			{
				dep = module_deplist->head->data;
				sfree(dep->name);
				sfree(dep);
				mowgli_node_delete(module_deplist->head, module_deplist);
				mowgli_list_free(module_deplist);
				return;
			}
		}
		else if (dep->can_unload == MODULE_UNLOAD_CAPABILITY_RELOAD_ONLY
			&& reloading_semipermanent_module == NULL)
		{
			reloading_semipermanent_module = dep;
		}
	}

	if (reloading_semipermanent_module)
	{
		/* If we're reloading a semi-permanent module (reload only; no unload), then there's
		 * a chance that we'll abort if the module fails to load again. Save the DB beforehand
		 * just in case
		 */
		slog(LG_INFO, "UPDATE (due to reload of module \2%s\2): \2%s\2",
				reloading_semipermanent_module->name, get_oper_name(si));
		wallops("Updating database by request of \2%s\2.", get_oper_name(si));
		db_save(NULL, DB_SAVE_BG_IMPORTANT);
	}

	module_unload(m, MODULE_UNLOAD_INTENT_RELOAD);

	while (module_deplist->head != NULL)
	{
		struct module *t;
		n = module_deplist->head;
		struct module_dependency *dep = n->data;

		t = module_load(dep->name);

		if (t != NULL)
		{
			logcommand(si, CMDLOG_ADMIN, "MODRELOAD: \2%s\2 (from \2%s\2)", dep->name, module);
			command_success_nodata(si, _("Module \2%s\2 reloaded (from \2%s\2)."), dep->name, module);
		}
		else
		{
			if (dep->can_unload != MODULE_UNLOAD_CAPABILITY_OK)
			{
				// Failed to reload a module that can't be unloaded. Abort.
				command_fail(si, fault_nosuch_target, _(
						"Module \2%s\2 failed to reload, and does not allow unloading. "
						"Shutting down to avoid data loss."), dep->name);
				slog(LG_ERROR, "MODRELOAD:ERROR: \2%s\2 failed to reload and does not allow unloading. "
						"Shutting down to avoid data loss.", dep->name);
				sendq_flush(curr_uplink->conn);

				exit(EXIT_FAILURE);
			}

			command_fail(si, fault_nosuch_target, _("Module \2%s\2 failed to reload."), dep->name);
			slog(LG_ERROR, "MODRELOAD:ERROR: \2%s\2 tried to reload \2%s\2 (from \2%s\2), operation failed.", get_oper_name(si), dep->name, module);
		}

		sfree(dep->name);
		sfree(dep);
		mowgli_node_delete(n, module_deplist);
	}
	mowgli_list_free(module_deplist);

	if (conf_need_rehash)
	{
		logcommand(si, CMDLOG_ADMIN, "REHASH (MODRELOAD)");
		wallops("Rehashing \2%s\2 to complete module reload by request of \2%s\2.", config_file, get_oper_name(si));
		if (!conf_rehash())
			command_fail(si, fault_nosuch_target, _("REHASH of \2%s\2 failed. Please correct any errors in the file and try again."), config_file);
	}
}

static struct command os_modreload = {
	.name           = "MODRELOAD",
	.desc           = N_("Reloads a module."),
	.access         = PRIV_ADMIN,
	.maxparc        = 20,
	.cmd            = &os_cmd_modreload,
	.help           = { .path = "oservice/modreload" },
};

static void
mod_init(struct module ATHEME_VATTR_UNUSED *const restrict m)
{
	service_named_bind_command("operserv", &os_modreload);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	service_named_unbind_command("operserv", &os_modreload);
}

SIMPLE_DECLARE_MODULE_V1("operserv/modreload", MODULE_UNLOAD_CAPABILITY_OK)
