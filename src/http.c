/**
 * @file http.c  Baresip WebRTC demo -- HTTP functions
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


int http_reply_json(struct http_conn *conn, const char *sessid,
		    const struct odict *od)
{
	const char *ctype = "application/json";
	char *buf = NULL;
	int err;

	if (!conn)
		return EINVAL;

	err = re_sdprintf(&buf, "%H", json_encode_odict, od);
	if (err)
		goto out;

	err = http_reply(conn, 201, "Created",
			 "Content-Type: %s\r\n"
			 "Content-Length: %zu\r\n"
			 "Access-Control-Allow-Origin: *\r\n"
			 "Session-ID: %s\r\n"
			 "\r\n"
			 "%s",
			 ctype, str_len(buf), sessid, buf);

 out:
	mem_deref(buf);

	return err;
}


/*
 * format:
 *
 * {
 *   "type" : "answer",
 *   "sdp" : "v=0\r\ns=-\r\n..."
 * }
 *
 * specification:
 *
 * https://developer.mozilla.org/en-US/docs/Web/API/RTCSessionDescription
 *
 * NOTE: currentLocalDescription
 */
int http_reply_descr(struct http_conn *conn, const char *sessid,
		     enum sdp_type type, struct mbuf *mb_sdp)
{
	struct odict *od = NULL;
	int err;

	if (!conn || !mb_sdp)
		return EINVAL;

	err = session_description_encode(&od, type, mb_sdp);
	if (err)
		goto out;

	err = http_reply_json(conn, sessid, od);
	if (err)
		goto out;

 out:
	mem_deref(od);

	return err;
}
