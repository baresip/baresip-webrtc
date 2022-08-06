/**
 * @file demo.h  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */


/*
 * NOTE: API under development
 */


struct session {
	struct le le;
	struct peer_connection *pc;
	struct http_conn *conn_pending;
	char id[4];
};

struct session *session_lookup(const struct list *sessl,
			       const struct http_msg *msg);
void session_close(struct session *sess, int err);


/*
 * Demo
 */

int  demo_init(const char *ice_server,
	       const char *stun_user, const char *stun_pass);
void demo_close(void);
