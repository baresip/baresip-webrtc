/**
 * @file demo.h  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */


/*
 * NOTE: API under development
 */


/*
 * HTTP
 */

int http_reply_descr(struct http_conn *conn, const char *sessid,
		     enum sdp_type type, struct mbuf *mb_sdp);
const char *http_extension_to_mimetype(const char *ext);


/*
 * Demo
 */

int  demo_init(const char *ice_server,
	       const char *stun_user, const char *stun_pass);
void demo_close(void);
