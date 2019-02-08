/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2010 Atheme Project (http://atheme.org/)
 * Copyright (C) 2018-2019 Atheme Development Group (https://atheme.github.io/)
 */

#include "atheme.h"
#include "groupserv.h"

#define GDBV_VERSION            4

#define METADATA_PERSIST_NAME   "atheme.groupserv.main.persist"
#define METADATA_PERSIST_VERS   1

struct groupserv_persist_record
{
	mowgli_heap_t * groupacs_heap;
	mowgli_heap_t * mygroup_heap;
	unsigned int    version;
};

static mowgli_heap_t *groupacs_heap = NULL;
static mowgli_heap_t *mygroup_heap = NULL;
static struct service *groupsvs = NULL;

static mowgli_eventloop_timer_t *mygroup_expire_timer = NULL;
static struct groupserv_config gs_config;

static unsigned int loading_gdbv = -1;
static unsigned int their_ga_all = 0;

static mowgli_list_t *
myentity_get_membership_list(struct myentity *const restrict mt)
{
	return_null_if_fail(mt != NULL);

	mowgli_list_t *const elist = privatedata_get(mt, "groupserv:membership");

	if (elist)
		return elist;

	mowgli_list_t *const nlist = mowgli_list_create();

	return_null_if_fail(nlist != NULL);

	(void) privatedata_set(mt, "groupserv:membership", nlist);

	return nlist;
}

static void
groupacs_destroy(void *const restrict ga)
{
	return_if_fail(ga != NULL);

	(void) metadata_delete_all(ga);
	(void) mowgli_heap_free(groupacs_heap, ga);
}

static struct groupacs *
groupacs_add(struct mygroup *const mg, struct myentity *const mt, const unsigned int flags)
{
	return_null_if_fail(mg != NULL);
	return_null_if_fail(mt != NULL);

	struct groupacs *const ga = mowgli_heap_alloc(groupacs_heap);

	return_null_if_fail(ga != NULL);

	(void) atheme_object_init(atheme_object(ga), NULL, &groupacs_destroy);

	ga->mg = mg;
	ga->mt = mt;
	ga->flags = flags;

	(void) mowgli_node_add(ga, &ga->gnode, &mg->acs);
	(void) mowgli_node_add(ga, &ga->unode, myentity_get_membership_list(mt));

	return ga;
}

static struct groupacs *
groupacs_find(struct mygroup *const mg, const struct myentity *const mt, const unsigned int flags, const bool allow_recurse)
{
	return_null_if_fail(mg != NULL);
	return_null_if_fail(mt != NULL);

	struct groupacs *out = NULL;
	const mowgli_node_t *n;

	mg->visited = true;

	MOWGLI_ITER_FOREACH(n, mg->acs.head)
	{
		struct groupacs *const ga = n->data;

		if (isgroup(ga->mt) && allow_recurse && ! group(ga->mt)->visited)
		{
			if (groupacs_find(group(ga->mt), mt, flags, allow_recurse))
			{
				out = ga;
				break;
			}
		}
		else
		{
			if (ga->mt != mt || ga->mg != mg)
				continue;

			if (flags && ! (ga->flags & flags))
				continue;

			out = ga;
			break;
		}
	}

	mg->visited = false;

	return out;
}

static void
groupacs_delete(struct mygroup *const mg, struct myentity *const mt)
{
	return_if_fail(mg != NULL);
	return_if_fail(mt != NULL);

	struct groupacs *const ga = groupacs_find(mg, mt, 0, false);

	if (! ga)
		return;

	(void) mowgli_node_delete(&ga->gnode, &mg->acs);
	(void) mowgli_node_delete(&ga->unode, myentity_get_membership_list(mt));
	(void) atheme_object_unref(ga);
}

static unsigned int
groupacs_sourceinfo_flags(struct mygroup *const restrict mg, const struct sourceinfo *const restrict si)
{
	return_val_if_fail(mg != NULL, 0);
	return_val_if_fail(si != NULL, 0);

	const struct groupacs *const ga = groupacs_find(mg, entity(si->smu), 0, true);

	return ((ga != NULL) ? ga->flags : 0);
}

static bool
groupacs_sourceinfo_has_flag(struct mygroup *const restrict mg, const struct sourceinfo *const restrict si,
                             const unsigned int flag)
{
	return_val_if_fail(mg != NULL, false);
	return_val_if_fail(si != NULL, false);

	return ((groupacs_find(mg, entity(si->smu), flag, true) != NULL) ? true : false);
}

static unsigned int
gs_flags_parser(const char *const restrict flag_string, const bool allow_subtraction, unsigned int flags)
{
	return_val_if_fail(flag_string != NULL, 0);

	bool subtracting = false;
	const char *c = flag_string;

	unsigned int flag;
	unsigned char n;

	while (*c)
	{
		flag = 0;
		n = 0;

		switch (*c)
		{
		case '+':
			subtracting = false;
			break;
		case '-':
			if (allow_subtraction)
				subtracting = true;
			break;
		case '*':
			if (subtracting)
			{
				flags = 0;
			}
			else
			{
				// preserve existing flags except GA_BAN
				flags |= GA_ALL;
				flags &= ~GA_BAN;
			}
			break;
		default:
			while (ga_flags[n].ch && ! flag)
			{
				if (ga_flags[n].ch == *c)
					flag = ga_flags[n].value;
				else
					n++;
			}
			if (! flag)
				break;
			if (subtracting)
				flags &= ~flag;
			else
				flags |= flag;
			break;
		}

		c++;
	}

	return flags;
}

static bool
mygroup_allow_foundership(struct myentity ATHEME_VATTR_UNUSED *const restrict mt)
{
	return true;
}

static bool
mygroup_can_register_channel(struct myentity *const restrict mt)
{
	return_val_if_fail(mt != NULL, false);

	const struct mygroup *const mg = group(mt);

	return_val_if_fail(mg != NULL, false);

	return ((mg->flags & MG_REGNOLIMIT) ? true : false);
}

static struct chanacs *
mygroup_chanacs_match_entity(struct chanacs *const restrict ca, struct myentity *const restrict mt)
{
	return_null_if_fail(ca != NULL);
	return_null_if_fail(mt != NULL);

	struct mygroup *const mg = group(ca->entity);

	return_null_if_fail(mg != NULL);

	if (! isuser(mt))
		return NULL;

	return ((groupacs_find(mg, mt, GA_CHANACS, true) != NULL) ? ca : NULL);
}

static void
mygroup_rename(struct mygroup *const restrict mg, const char *const restrict name)
{
	return_if_fail(mg != NULL);
	return_if_fail(name != NULL);
	return_if_fail(strlen(name) <= GROUPLEN);

	(void) myentity_del(entity(mg));
	(void) strshare_unref(entity(mg)->name);

	entity(mg)->name = strshare_get(name);

	(void) myentity_put(entity(mg));
}

static void
mygroup_set_chanacs_validator(struct myentity *const restrict mt)
{
	static const struct entity_chanacs_validation_vtable mygroup_chanacs_validate = {
		.allow_foundership      = &mygroup_allow_foundership,
		.can_register_channel   = &mygroup_can_register_channel,
		.match_entity           = &mygroup_chanacs_match_entity,
	};

	mt->chanacs_validate = &mygroup_chanacs_validate;
}

static unsigned int
mygroup_count_flag(const struct mygroup *const restrict mg, const unsigned int flag)
{
	return_val_if_fail(mg != NULL, 0);

	if (! flag)
		return MOWGLI_LIST_LENGTH(&mg->acs);

	const mowgli_node_t *n;
	unsigned int count = 0;

	MOWGLI_ITER_FOREACH(n, mg->acs.head)
	{
		const struct groupacs *const ga = n->data;

		if (ga->flags & flag)
			count++;
	}

	return count;
}

static void
mygroup_delete(void *const restrict ptr)
{
	return_if_fail(ptr != NULL);

	struct mygroup *const mg = ptr;

	(void) myentity_del(entity(mg));

	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, mg->acs.head)
	{
		struct groupacs *const ga = n->data;

		(void) mowgli_node_delete(&ga->gnode, &mg->acs);
		(void) mowgli_node_delete(&ga->unode, myentity_get_membership_list(ga->mt));
		(void) atheme_object_unref(ga);
	}

	(void) metadata_delete_all(mg);
	(void) strshare_unref(entity(mg)->name);
	(void) mowgli_heap_free(mygroup_heap, mg);
}

static struct mygroup *
mygroup_find(const char *const restrict name)
{
	return_null_if_fail(name != NULL);

	struct myentity *const mg = myentity_find(name);

	if (! mg)
		return NULL;

	if (! isgroup(mg))
		return NULL;

	return group(mg);
}

static const char *
mygroup_founder_names(const struct mygroup *const restrict mg)
{
	return_val_if_fail(mg != NULL, "");

	// 384 + NULL; least possibility of truncation when sent over wire, similar to 390 for TOPIC
	static char names[385];
	size_t written = 0;

	(void) memset(names, 0x00, sizeof names);

	const mowgli_node_t *n;

	MOWGLI_ITER_FOREACH(n, mg->acs.head)
	{
		const struct groupacs *const ga = n->data;

		if (ga->mt && (ga->flags & GA_FOUNDER))
		{
			if (written)
			{
				if ((written + 2) >= sizeof names)
					return names;

				names[written++] = ',';
				names[written++] = ' ';
			}

			const size_t namelen = strlen(ga->mt->name);

			if ((written + namelen) >= sizeof names)
			{
				if ((written + 3) < sizeof names)
					(void) memcpy(names + written, "...", 3);

				return names;
			}

			(void) memcpy(names + written, ga->mt->name, namelen);

			written += namelen;
		}
	}

	return names;
}

static unsigned int
myentity_count_group_flag(struct myentity *const restrict mt, const unsigned int flagset)
{
	return_val_if_fail(mt != NULL, 0);

	unsigned int count = 0;

	const mowgli_list_t *const members = myentity_get_membership_list(mt);
	const mowgli_node_t *n;

	MOWGLI_ITER_FOREACH(n, members->head)
	{
		const struct groupacs *const ga = n->data;

		if (ga->mt == mt && (ga->flags & flagset))
			count++;
	}

	return count;
}

static void
remove_group_chanacs(struct mygroup *const restrict mg)
{
	return_if_fail(mg != NULL);

	const char *const msg1 = _("SUCCESSION: \2%s\2 to \2%s\2 from \2%s\2");
	const char *const msg2 = _("%s: giving channel \2%s\2 to \2%s\2 (unused %lus, founder \2%s\2, chanacs %zu)");
	const char *const msg3 = _("Foundership changed to \2%s\2 because \2%s\2 was dropped.");
	const char *const msg4 = _("You are now founder on \2%s\2 (as \2%s\2) because \2%s\2 was dropped.");
	const char *const msg5 = _("DELETE: \2%s\2 from \2%s\2");
	const char *const msg6 = _("%s: deleting channel \2%s\2 (unused %lus, founder \2%s\2, chanacs %zu)");

	mowgli_node_t *n, *tn;

	// Kill all their channels and chanacs
	MOWGLI_ITER_FOREACH_SAFE(n, tn, entity(mg)->chanacs.head)
	{
		struct chanacs *const ca = n->data;
		struct mychan *const mc = ca->mychan;
		struct myuser *successor;

		continue_if_fail(mc != NULL);

		const unsigned long unused = (unsigned long) (CURRTIME - mc->used);

		// Attempt succession
		if ((ca->level & CA_FOUNDER) && mychan_num_founders(mc) == 1 && (successor = mychan_pick_successor(mc)))
		{
			(void) slog(LG_INFO, msg1, mc->name, entity(successor)->name, entity(mg)->name);
			(void) slog(LG_VERBOSE, msg2, __func__, mc->name, entity(successor)->name, unused,
			                        entity(mg)->name, MOWGLI_LIST_LENGTH(&mc->chanacs));

			if (chansvs.me != NULL)
			{
				(void) verbose(mc, msg3, entity(successor)->name, entity(mg)->name);
				(void) myuser_notice(chansvs.nick, successor, msg4, mc->name, entity(successor)->name,
				                     entity(mg)->name);
			}

			(void) chanacs_change_simple(mc, entity(successor), NULL, CA_FOUNDER_0, 0, NULL);
			(void) atheme_object_unref(ca);
		}
		else if ((ca->level & CA_FOUNDER) && mychan_num_founders(mc) == 1)
		{
			// No successor found -- destroy the channel

			(void) slog(LG_INFO, msg5, mc->name, entity(mg)->name);
			(void) slog(LG_VERBOSE, msg6, __func__, mc->name, unused, entity(mg)->name,
			                        MOWGLI_LIST_LENGTH(&mc->chanacs));

			(void) hook_call_channel_drop(mc);

			if (mc->chan && ! (mc->chan->flags & CHAN_LOG))
				(void) part(mc->name, chansvs.nick);

			// Destroying mychan will destroy all its chanacs including this one
			(void) atheme_object_unref(mc);
		}
		else	// Not a founder, or not the only founder -- do nothing
			(void) atheme_object_unref(ca);
	}
}

static void
mygroup_expire(void ATHEME_VATTR_UNUSED *const restrict data)
{
	struct myentity *mt;
	struct myentity_iteration_state state;

	MYENTITY_FOREACH_T(mt, &state, ENT_GROUP)
	{
		continue_if_fail(mt != NULL);

		struct mygroup *mg = group(mt);

		continue_if_fail(mg != NULL);

		if (! mygroup_count_flag(mg, GA_FOUNDER))
		{
			(void) remove_group_chanacs(mg);
			(void) atheme_object_unref(mg);
		}
	}
}

static void
grant_channel_access_hook(struct user *const restrict u)
{
	return_if_fail(u != NULL);

	struct myuser *const mu = u->myuser;

	return_if_fail(mu != NULL);

	if (! chansvs.me)
		return;

	const mowgli_list_t *const members = myentity_get_membership_list(entity(mu));
	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, members->head)
	{
		struct groupacs *const ga = n->data;

		continue_if_fail(ga != NULL);
		continue_if_fail(ga->mg != NULL);

		if (! (ga->flags & GA_CHANACS))
			continue;

		MOWGLI_ITER_FOREACH(n, entity(ga->mg)->chanacs.head)
		{
			struct chanacs *const ca = n->data;

			continue_if_fail(ca != NULL);
			continue_if_fail(ca->mychan != NULL);

			struct mychan *const mc = ca->mychan;
			struct channel *const c = mc->chan;

			if (! c)
				continue;

			struct chanuser *const cu = chanuser_find(c, u);

			if (! cu)
				continue;

			if ((ca->level & CA_AKICK) && ! (ca->level & CA_EXEMPT))
			{
				// Stay on channel if this would empty it -- jilles
				if ((c->nummembers - c->numsvcmembers) == 1)
				{
					mc->flags |= MC_INHABIT;

					if (! c->numsvcmembers)
						(void) join(c->name, chansvs.nick);
				}

				(void) ban(chansvs.me->me, c, u);
				(void) remove_ban_exceptions(chansvs.me->me, c, u);
				(void) kick(chansvs.me->me, c, u, _("User is banned from this channel"));
				continue;
			}

			if (ca->level & CA_USEDUPDATE)
				mc->used = CURRTIME;

			if ((mc->flags & MC_NOOP) || (mu->flags & MU_NOOP))
				continue;

			if (ircd->uses_owner && ! (cu->modes & ircd->owner_mode) && (ca->level & CA_AUTOOP)
			    && (ca->level & CA_USEOWNER))
			{
				(void) modestack_mode_param(chansvs.nick, c, MTYPE_ADD, ircd->owner_mchar[1],
				                            CLIENT_NAME(u));

				cu->modes |= ircd->owner_mode;
			}

			if (ircd->uses_protect && ! (cu->modes & ircd->protect_mode)
			    && ! (ircd->uses_owner && (cu->modes & ircd->owner_mode))
			    && (ca->level & CA_AUTOOP) && (ca->level & CA_USEPROTECT))
			{
				(void) modestack_mode_param(chansvs.nick, c, MTYPE_ADD, ircd->protect_mchar[1],
				                            CLIENT_NAME(u));

				cu->modes |= ircd->protect_mode;
			}

			if (! (cu->modes & CSTATUS_OP) && (ca->level & CA_AUTOOP))
			{
				(void) modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'o',
				                            CLIENT_NAME(u));

				cu->modes |= CSTATUS_OP;
			}

			if (ircd->uses_halfops && ! (cu->modes & (CSTATUS_OP | ircd->halfops_mode))
			    && (ca->level & CA_AUTOHALFOP))
			{
				(void) modestack_mode_param(chansvs.nick, c, MTYPE_ADD, ircd->halfops_mchar[1],
				                            CLIENT_NAME(u));

				cu->modes |= ircd->halfops_mode;
			}

			if (! (cu->modes & (CSTATUS_OP | ircd->halfops_mode | CSTATUS_VOICE))
			    && (ca->level & CA_AUTOVOICE))
			{
				(void) modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'v',
				                            CLIENT_NAME(u));

				cu->modes |= CSTATUS_VOICE;
			}
		}
	}
}

static void
user_info_hook(hook_user_req_t *const restrict req)
{
	return_if_fail(req != NULL);
	return_if_fail(req->mu != NULL);
	return_if_fail(req->si != NULL);

	char groups[385]; // 384 + NULL; least possibility of truncation when sent over wire
	size_t written = 0;

	(void) memset(groups, 0x00, sizeof groups);

	const mowgli_list_t *const members = myentity_get_membership_list(entity(req->mu));
	const mowgli_node_t *n;

	MOWGLI_ITER_FOREACH(n, members->head)
	{
		const struct groupacs *const ga = n->data;
		struct mygroup *const mg = ga->mg;

		if (ga->flags & GA_BAN)
			continue;

		if ((mg->flags & MG_PUBLIC) || req->si->smu == req->mu || has_priv(req->si, PRIV_GROUP_AUSPEX))
		{
			if (written)
			{
				if ((written + 2) >= sizeof groups)
					break;

				groups[written++] = ',';
				groups[written++] = ' ';
			}

			const size_t grouplen = strlen(entity(mg)->name);

			if ((written + grouplen) >= sizeof groups)
			{
				if ((written + 3) < sizeof groups)
					(void) memcpy(groups + written, "...", 3);

				break;
			}

			(void) memcpy(groups + written, entity(mg)->name, grouplen);

			written += grouplen;
		}
	}

	if (written)
		(void) command_success_nodata(req->si, _("Groups     : %s"), groups);
}

static void
sasl_may_impersonate_hook(hook_sasl_may_impersonate_t *const restrict req)
{
	return_if_fail(req != NULL);
	return_if_fail(req->source_mu != NULL);
	return_if_fail(req->target_mu != NULL);

	// if the request is already granted, don't bother doing any of this.
	if (req->allowed)
		return;

	const mowgli_list_t *const members = myentity_get_membership_list(entity(req->target_mu));
	const mowgli_node_t *n;

	MOWGLI_ITER_FOREACH(n, members->head)
	{
		const struct groupacs *const ga = n->data;
		struct mygroup *const mg = ga->mg;

		char priv[BUFSIZE];
		(void) snprintf(priv, sizeof priv, PRIV_IMPERSONATE_ENTITY_FMT, entity(mg)->name);

		if (has_priv_myuser(req->source_mu, priv))
		{
			req->allowed = true;
			return;
		}
	}
}

static void
myuser_delete_hook(struct myuser *const restrict mu)
{
	return_if_fail(mu != NULL);

	mowgli_list_t *const members = myentity_get_membership_list(entity(mu));
	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, members->head)
	{
		const struct groupacs *const ga = n->data;

		(void) groupacs_delete(ga->mg, ga->mt);
	}

	(void) mowgli_list_free(members);
}

static void
osinfo_hook(struct sourceinfo *const restrict si)
{
	return_if_fail(si != NULL);

	const char *const msg1 = _("Maximum number of groups one user can own: %u");
	const char *const msg2 = _("Maximum number of ACL entries allowed for one group: %u");
	const char *const msg3 = _("Are open groups allowed: %s");
	const char *const msg4 = _("Default joinflags for open groups: %s");

	(void) command_success_nodata(si, msg1, gs_config.maxgroups);
	(void) command_success_nodata(si, msg2, gs_config.maxgroupacs);
	(void) command_success_nodata(si, msg3, gs_config.enable_open_groups ? _("Yes") : _("No"));
	(void) command_success_nodata(si, msg4, gs_config.join_flags);
}

static struct mygroup *
mygroup_add_id(const char *const restrict id, const char *const restrict name)
{
	return_null_if_fail(name != NULL);

	struct mygroup *const mg = mowgli_heap_alloc(mygroup_heap);

	return_null_if_fail(mg != NULL);

	(void) memset(mg, 0x00, sizeof *mg);
	(void) atheme_object_init(atheme_object(mg), NULL, &mygroup_delete);

	entity(mg)->type = ENT_GROUP;
	entity(mg)->name = strshare_get(name);

	if (id && ! myentity_find_uid(id))
		(void) mowgli_strlcpy(entity(mg)->id, id, sizeof entity(mg)->id);

	(void) myentity_put(entity(mg));
	(void) mygroup_set_chanacs_validator(entity(mg));

	mg->regtime = CURRTIME;

	return mg;
}

static struct mygroup *
mygroup_add(const char *const restrict name)
{
	return mygroup_add_id(NULL, name);
}

static void
write_groupdb_hook(struct database_handle *const restrict db)
{
	return_if_fail(db != NULL);

	(void) db_start_row(db, "GDBV");
	(void) db_write_uint(db, GDBV_VERSION);
	(void) db_commit_row(db);

	(void) db_start_row(db, "GFA");
	(void) db_write_word(db, gflags_tostr(ga_flags, GA_ALL));
	(void) db_commit_row(db);

	struct myentity_iteration_state mestate;
	struct myentity *mt;

	MYENTITY_FOREACH_T(mt, &mestate, ENT_GROUP)
	{
		continue_if_fail(mt != NULL);
		struct mygroup *const mg = group(mt);
		continue_if_fail(mg != NULL);

		(void) db_start_row(db, "GRP");
		(void) db_write_word(db, entity(mg)->id);
		(void) db_write_word(db, entity(mg)->name);
		(void) db_write_time(db, mg->regtime);
		(void) db_write_word(db, gflags_tostr(mg_flags, mg->flags));
		(void) db_commit_row(db);

		if (atheme_object(mg)->metadata)
		{
			mowgli_patricia_iteration_state_t mpstate;
			const struct metadata *md;

			MOWGLI_PATRICIA_FOREACH(md, &mpstate, atheme_object(mg)->metadata)
			{
				(void) db_start_row(db, "MDG");
				(void) db_write_word(db, entity(mg)->name);
				(void) db_write_word(db, md->name);
				(void) db_write_str(db, md->value);
				(void) db_commit_row(db);
			}
		}
	}

	MYENTITY_FOREACH_T(mt, &mestate, ENT_GROUP)
	{
		continue_if_fail(mt != NULL);
		struct mygroup *const mg = group(mt);
		continue_if_fail(mg != NULL);

		mowgli_node_t *n;

		MOWGLI_ITER_FOREACH(n, mg->acs.head)
		{
			const struct groupacs *const ga = n->data;

			(void) db_start_row(db, "GACL");
			(void) db_write_word(db, entity(mg)->name);
			(void) db_write_word(db, ga->mt->name);
			(void) db_write_word(db, gflags_tostr(ga_flags, ga->flags));
			(void) db_commit_row(db);
		}
	}
}

static void
db_h_gdbv(struct database_handle *db, const char *type)
{
	loading_gdbv = db_sread_uint(db);
	slog(LG_INFO, "groupserv: opensex data schema version is %d.", loading_gdbv);

	their_ga_all = GA_ALL_OLD;
}

static void
db_h_gfa(struct database_handle *db, const char *type)
{
	const char *flags = db_sread_word(db);

	gflags_fromstr(ga_flags, flags, &their_ga_all);
	if (their_ga_all & ~GA_ALL)
	{
		slog(LG_ERROR, "db-h-gfa: losing flags %s from file", gflags_tostr(ga_flags, their_ga_all & ~GA_ALL));
	}
	if (~their_ga_all & GA_ALL)
	{
		slog(LG_ERROR, "db-h-gfa: making up flags %s not present in file", gflags_tostr(ga_flags, ~their_ga_all & GA_ALL));
	}
}

static void
db_h_grp(struct database_handle *db, const char *type)
{
	struct mygroup *mg;
	const char *uid = NULL;
	const char *name;
	time_t regtime;
	const char *flagset;

	if (loading_gdbv >= 4)
		uid = db_sread_word(db);

	name = db_sread_word(db);

	if (mygroup_find(name))
	{
		slog(LG_INFO, "db-h-grp: line %d: skipping duplicate group %s", db->line, name);
		return;
	}
	if (uid && myentity_find_uid(uid))
	{
		slog(LG_INFO, "db-h-grp: line %d: skipping group %s with duplicate UID %s", db->line, name, uid);
		return;
	}

	regtime = db_sread_time(db);

	mg = mygroup_add_id(uid, name);
	mg->regtime = regtime;

	if (loading_gdbv >= 3)
	{
		flagset = db_sread_word(db);

		if (!gflags_fromstr(mg_flags, flagset, &mg->flags))
			slog(LG_INFO, "db-h-grp: line %d: confused by flags: %s", db->line, flagset);
	}
}

static void
db_h_gacl(struct database_handle *db, const char *type)
{
	struct mygroup *mg;
	struct myentity *mt;
	unsigned int flags = GA_ALL;	// GDBV 1 entires had full access

	const char *name = db_sread_word(db);
	const char *entity = db_sread_word(db);
	const char *flagset;

	mg = mygroup_find(name);
	mt = myentity_find(entity);

	if (mg == NULL)
	{
		slog(LG_INFO, "db-h-gacl: line %d: groupacs for nonexistent group %s", db->line, name);
		return;
	}

	if (mt == NULL)
	{
		slog(LG_INFO, "db-h-gacl: line %d: groupacs for nonexistent entity %s", db->line, entity);
		return;
	}

	if (loading_gdbv >= 2)
	{
		flagset = db_sread_word(db);

		if (!gflags_fromstr(ga_flags, flagset, &flags))
			slog(LG_INFO, "db-h-gacl: line %d: confused by flags: %s", db->line, flagset);

		/* ACL view permission was added, so make up the permission (#279), but only if the database
		 * is from atheme 7.1 or earlier. --kaniini
		 */
		if (!(their_ga_all & GA_ACLVIEW) && ((flags & GA_ALL_OLD) == their_ga_all))
			flags |= GA_ACLVIEW;
	}

	groupacs_add(mg, mt, flags);
}

static void
db_h_mdg(struct database_handle *db, const char *type)
{
	const char *name = db_sread_word(db);
	const char *prop = db_sread_word(db);
	const char *value = db_sread_str(db);
	void *obj = NULL;

	obj = mygroup_find(name);

	if (obj == NULL)
	{
		slog(LG_INFO, "db-h-mdg: attempting to add %s property to non-existant object %s",
		     prop, name);
		return;
	}

	metadata_add(obj, prop, value);
}

static void
mod_init(struct module *const restrict m)
{
	if (! (groupsvs = service_add("groupserv", NULL)))
	{
		(void) slog(LG_ERROR, "%s: service_add() failed", m->name);
		goto fail;
	}
	if (! (mygroup_expire_timer = mowgli_timer_add(base_eventloop, "mygroup_expire", &mygroup_expire, NULL, 3600)))
	{
		(void) slog(LG_ERROR, "%s: mowgli_timer_add() failed", m->name);
		goto fail;
	}

	struct groupserv_persist_record *const rec = mowgli_global_storage_get(METADATA_PERSIST_NAME);

	if (rec)
	{
		if (rec->version != METADATA_PERSIST_VERS)
		{
			(void) slog(LG_ERROR, "%s: persist version unrecognised!", m->name);
			goto fail;
		}

		groupacs_heap = rec->groupacs_heap;
		mygroup_heap = rec->mygroup_heap;

		(void) mowgli_global_storage_free(METADATA_PERSIST_NAME);
		(void) sfree(rec);

		struct myentity *grp;
		struct myentity_iteration_state iter;

		MYENTITY_FOREACH_T(grp, &iter, ENT_GROUP)
			(void) mygroup_set_chanacs_validator(grp);
	}
	else
	{
		if (! (groupacs_heap = mowgli_heap_create(sizeof(struct groupacs), HEAP_GROUPACS, BH_NOW)))
		{
			(void) slog(LG_ERROR, "%s: mowgli_heap_create() failed", m->name);
			goto fail;
		}
		if (! (mygroup_heap = mowgli_heap_create(sizeof(struct mygroup), HEAP_GROUP, BH_NOW)))
		{
			(void) slog(LG_ERROR, "%s: mowgli_heap_create() failed", m->name);
			goto fail;
		}
	}

	(void) add_bool_conf_item("ENABLE_OPEN_GROUPS", &groupsvs->conf_table, 0, &gs_config.enable_open_groups, false);
	(void) add_dupstr_conf_item("JOIN_FLAGS", &groupsvs->conf_table, 0, &gs_config.join_flags, "+");
	(void) add_uint_conf_item("MAXGROUPS", &groupsvs->conf_table, 0, &gs_config.maxgroups, 0, 65535, 5);
	(void) add_uint_conf_item("MAXGROUPACS", &groupsvs->conf_table, 0, &gs_config.maxgroupacs, 0, 65535, 0);

	(void) db_register_type_handler("GDBV", &db_h_gdbv);
	(void) db_register_type_handler("GFA", &db_h_gfa);
	(void) db_register_type_handler("GRP", &db_h_grp);
	(void) db_register_type_handler("MDG", &db_h_mdg);
	(void) db_register_type_handler("GACL", &db_h_gacl);

	(void) hook_add_event("grant_channel_access");
	(void) hook_add_grant_channel_access(&grant_channel_access_hook);

	(void) hook_add_event("sasl_may_impersonate");
	(void) hook_add_sasl_may_impersonate(&sasl_may_impersonate_hook);

	(void) hook_add_event("db_write_pre_ca");
	(void) hook_add_db_write_pre_ca(&write_groupdb_hook);

	(void) hook_add_event("myuser_delete");
	(void) hook_add_myuser_delete(&myuser_delete_hook);

	(void) hook_add_event("operserv_info");
	(void) hook_add_operserv_info(&osinfo_hook);

	(void) hook_add_event("user_info");
	(void) hook_add_user_info(&user_info_hook);

	m->mflags |= MODFLAG_DBHANDLER;
	return;

fail:
	(void) slog(LG_ERROR, "%s: exiting to avoid data loss!", m->name);

	exit(EXIT_FAILURE);
}

static void
mod_deinit(const enum module_unload_intent intent)
{
	(void) service_delete(groupsvs);

	(void) mowgli_timer_destroy(base_eventloop, mygroup_expire_timer);

	(void) del_conf_item("MAXGROUPS", &groupsvs->conf_table);
	(void) del_conf_item("MAXGROUPACS", &groupsvs->conf_table);
	(void) del_conf_item("ENABLE_OPEN_GROUPS", &groupsvs->conf_table);
	(void) del_conf_item("JOIN_FLAGS", &groupsvs->conf_table);

	(void) db_unregister_type_handler("GDBV");
	(void) db_unregister_type_handler("GFA");
	(void) db_unregister_type_handler("GRP");
	(void) db_unregister_type_handler("MDG");
	(void) db_unregister_type_handler("GACL");

	(void) hook_del_grant_channel_access(&grant_channel_access_hook);
	(void) hook_del_sasl_may_impersonate(&sasl_may_impersonate_hook);
	(void) hook_del_db_write_pre_ca(&write_groupdb_hook);
	(void) hook_del_myuser_delete(&myuser_delete_hook);
	(void) hook_del_operserv_info(&osinfo_hook);
	(void) hook_del_user_info(&user_info_hook);

	switch (intent)
	{
		case MODULE_UNLOAD_INTENT_RELOAD:
		{
			struct groupserv_persist_record *const rec = smalloc(sizeof *rec);

			rec->groupacs_heap = groupacs_heap;
			rec->mygroup_heap = mygroup_heap;
			rec->version = METADATA_PERSIST_VERS;

			(void) mowgli_global_storage_put(METADATA_PERSIST_NAME, rec);
			break;
		}

		case MODULE_UNLOAD_INTENT_PERM:
			(void) mowgli_heap_destroy(groupacs_heap);
			(void) mowgli_heap_destroy(mygroup_heap);
			break;
	}
}

// Imported by all other GroupServ modules
extern const struct groupserv_core_symbols groupserv_core_symbols;
const struct groupserv_core_symbols groupserv_core_symbols = {
	.config                         = &gs_config,
	.groupsvs                       = &groupsvs,
	.groupacs_add                   = &groupacs_add,
	.groupacs_delete                = &groupacs_delete,
	.groupacs_find                  = &groupacs_find,
	.groupacs_sourceinfo_flags      = &groupacs_sourceinfo_flags,
	.groupacs_sourceinfo_has_flag   = &groupacs_sourceinfo_has_flag,
	.gs_flags_parser                = &gs_flags_parser,
	.mygroup_add_id                 = &mygroup_add_id,
	.mygroup_add                    = &mygroup_add,
	.mygroup_count_flag             = &mygroup_count_flag,
	.mygroup_find                   = &mygroup_find,
	.mygroup_founder_names          = &mygroup_founder_names,
	.mygroup_rename                 = &mygroup_rename,
	.myentity_count_group_flag      = &myentity_count_group_flag,
	.myentity_get_membership_list   = &myentity_get_membership_list,
	.remove_group_chanacs           = &remove_group_chanacs,
};

SIMPLE_DECLARE_MODULE_V1("groupserv/main", MODULE_UNLOAD_CAPABILITY_RELOAD_ONLY)
