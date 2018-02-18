/*
 * Copyright (C) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Data structures for connected clients.
 */

#ifndef USERS_H
#define USERS_H

struct user_
{
	struct atheme_object parent;

	stringref nick;
	stringref user;
	stringref host; /* Real host */
	stringref gecos;
	stringref chost; /* Cloaked host */
	stringref vhost; /* Visible host */
	stringref uid; /* Used for TS6, P10, IRCNet ircd. */
	stringref ip;

	mowgli_list_t channels;

	server_t *server;
	myuser_t *myuser;

	unsigned int offenses;
	unsigned int msgs; /* times FLOOD_MSGS_FACTOR */
	time_t lastmsg;

	unsigned int flags;

	time_t ts;

	mowgli_node_t snode; /* for server_t.userlist */

	char *certfp; /* client certificate fingerprint */
};

#define FLOOD_MSGS_FACTOR 256

#define UF_AWAY        0x00000002
#define UF_INVIS       0x00000004
#define UF_IRCOP       0x00000010
#define UF_ADMIN       0x00000020
#define UF_SEENINFO    0x00000080
#define UF_IMMUNE      0x00000100 /* user is immune from kickban, don't bother enforcing akicks */
#define UF_HIDEHOSTREQ 0x00000200 /* host hiding requested */
#define UF_SOPER_PASS  0x00000400 /* services oper pass entered */
#define UF_DOENFORCE   0x00000800 /* introduce enforcer when nick changes */
#define UF_ENFORCER    0x00001000 /* this is an enforcer client */
#define UF_WASENFORCED 0x00002000 /* this user was FNCed once already */
#define UF_DEAF        0x00004000 /* user does not receive channel msgs */
#define UF_SERVICE     0x00008000 /* user is a service (e.g. +S on charybdis) */
#define UF_KLINESENT   0x00010000 /* we've sent a kline for this user */

#define CLIENT_NAME(user)	((user)->uid != NULL ? (user)->uid : (user)->nick)

typedef struct {
	user_t *u;		/* User in question. Write NULL here if you delete the user. */
	const char *oldnick;	/* Previous nick for nick changes. u->nick is the new nick. */
} hook_user_nick_t;

typedef struct {
	user_t *const u;
	const char *comment;
} hook_user_delete_t;

/* function.c */
extern bool is_ircop(user_t *user);
extern bool is_admin(user_t *user);
extern bool is_internal_client(user_t *user);
extern bool is_autokline_exempt(user_t *user);
extern bool is_service(user_t *user);

/* users.c */
extern mowgli_patricia_t *userlist;
extern mowgli_patricia_t *uidlist;

extern void init_users(void);

extern user_t *user_add(const char *nick, const char *user, const char *host, const char *vhost, const char *ip, const char *uid, const char *gecos, server_t *server, time_t ts);
extern void user_delete(user_t *u, const char *comment);
extern user_t *user_find(const char *nick);
extern user_t *user_find_named(const char *nick);
extern void user_changeuid(user_t *u, const char *uid);
extern bool user_changenick(user_t *u, const char *nick, time_t ts);
extern void user_mode(user_t *user, const char *modes);
extern void user_sethost(user_t *source, user_t *target, const char *host);
extern const char *user_get_umodestr(user_t *u);
extern bool user_is_channel_banned(user_t *u, char ban_type);

/* uid.c */
extern void init_uid(void);
extern const char *uid_get(void);

#endif
