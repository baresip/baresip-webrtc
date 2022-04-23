/**
 * @file http.c  Baresip WebRTC demo -- HTTP functions
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


int http_reply_fmt(struct http_conn *conn, const char *ctype,
		   const char *fmt, ...)
{
	char *buf = NULL;
	va_list ap;
	int err;

	if (!conn || !ctype || !fmt)
		return EINVAL;

	va_start(ap, fmt);
	err = re_vsdprintf(&buf, fmt, ap);
	va_end(ap);

	if (err)
		return err;

	info("demo: reply: %s\n", ctype);

	err = http_reply(conn, 200, "OK",
			 "Content-Type: %s\r\n"
			 "Content-Length: %zu\r\n"
			 "Access-Control-Allow-Origin: *\r\n"
			 "\r\n"
			 "%s",
			 ctype, str_len(buf), buf);

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
int http_reply_descr(struct http_conn *conn, enum sdp_type type,
		     struct mbuf *mb_sdp)
{
	struct odict *od = NULL;
	int err;

	err = session_description_encode(&od, type, mb_sdp);
	if (err)
		goto out;

	http_reply_fmt(conn, "application/json",
		       "%H", json_encode_odict, od);

 out:
	mem_deref(od);

	return err;
}
