/**
 * @file demo.c  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "demo.h"


/*
  TODO:

  - move module to an application using libbaresip ?
  - add support for video
  - add support for multiple sessions
  - convert HTTP content to JSON?
 */

enum {HTTP_PORT = 9000};


static struct http_sock *httpsock;
static struct http_conn *conn_pending;
static struct rtcsession *sess;


static void reply(struct http_conn *conn, struct mbuf *mb)
{
	http_reply(conn, 200, "OK",
		   "Content-Type: text/plain;charset=UTF-8\r\n"
		   "Content-Length: %zu\r\n"
		   "Access-Control-Allow-Origin: *\r\n"
		   "\r\n"
		   "%b",
		   mb->end,
		   mb->buf, mb->end);
}


static void session_gather_handler(void *arg)
{
	struct mbuf *mb_answer = NULL;
	int err;

	info("demo: session gathered.\n");

	err = rtcsession_encode_answer(sess, &mb_answer);
	if (err)
		goto out;

	reply(conn_pending, mb_answer);

	err = rtcsession_start_ice(sess);
	if (err) {
		warning("demo: failed to start ice (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(mb_answer);
}


static void session_estab_handler(struct stream *strm, void *arg)
{
	int err;

	(void)arg;

	err = rtcsession_start_audio(sess);
	if (err) {
		warning("demo: could not start audio (%m)\n", err);
	}
}


static void session_close_handler(int err, void *arg)
{
	(void)arg;

	warning("demo: session closed (%m)\n", err);

	/* todo: notify client that session was closed */
	sess = mem_deref(sess);
}


static int create_session(struct mbuf *offer)
{
	const struct mnat *mnat;
	const struct menc *menc;
	struct sa laddr;
	struct config config = *conf_config();
	struct rtcsession_param param = {
		.audio = true,
		.video = false
	};
	int err;

	/* override default config */
	config.avt.rtcp_mux = true;

	sa_set_str(&laddr, "127.0.0.1", 0);

	mnat = mnat_find(baresip_mnatl(), "ice");
	if (!mnat) {
		warning("demo: medianat 'ice' not found\n");
		err = ENOENT;
		goto out;
	}

	menc = menc_find(baresip_mencl(), "dtls_srtp");
	if (!menc) {
		warning("demo: mediaenc 'dtls-srtp' not found\n");
		err = ENOENT;
		goto out;
	}

	if (sess) {
		err = EBUSY;
		goto out;
	}

	/* create a new session object, send SDP to it */
	err = rtcsession_create(&sess, &config, &param,
				baresip_aucodecl(),
				&laddr,
				offer, mnat, menc,
				"stun.l.google.com", 19302,
				session_gather_handler,
				session_estab_handler,
				session_close_handler, NULL);
	if (err) {
		warning("demo: session alloc failed (%m)\n", err);
		goto out;
	}

 out:
	return err;
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	struct pl path = PL("/index.html");
	struct mbuf *mb;
	char *buf = NULL;
	int err = 0;
	(void)arg;

	info("demo: request: met=%r, path=%r, prm=%r\n",
	     &msg->met, &msg->path, &msg->prm);

	mb = mbuf_alloc(8192);
	if (!mb)
		return;

	if (msg->path.l > 1)
		path = msg->path;

	if (0 == pl_strcasecmp(&msg->met, "GET")) {

		err = re_sdprintf(&buf, "./www%r", &path);
		if (err)
			goto out;

		err = load_file(mb, buf);
		if (err) {
			info("demo: not found: %s\n", buf);
			http_ereply(conn, 404, "Not Found");
			goto out;
		}

		info("demo: loaded file '%s', %zu bytes\n", buf, mb->end);

		http_reply(conn, 200, "OK",
			   "Content-Type: text/html;charset=UTF-8\r\n"
			   "Content-Length: %zu\r\n"
			   "Access-Control-Allow-Origin: *\r\n"
			   "\r\n"
			   "%b",
			   mb->end,
			   mb->buf, mb->end);
	}
	else if (0 == pl_strcasecmp(&msg->met, "POST") &&
		 0 == pl_strcasecmp(&msg->path, "/call")) {

		err = create_session(msg->mb);
		if (err)
			goto out;

		/* async reply */
		mem_deref(conn_pending);
		conn_pending = mem_ref(conn);
	}
	else if (0 == pl_strcasecmp(&msg->met, "POST") &&
		 0 == pl_strcasecmp(&msg->path, "/hangup")) {

		info("demo: hangup\n");

		sess = mem_deref(sess);

		http_reply(conn, 200, "OK",
			   "Content-Length: 0\r\n"
			   "Access-Control-Allow-Origin: *\r\n"
			   "\r\n");
	}
	else {
		http_ereply(conn, 404, "Not Found");
	}

 out:
	if (err)
		http_ereply(conn, 500, "Server Error");

	mem_deref(buf);
	mem_deref(mb);
}


int demo_init(void)
{
	struct sa laddr;
	int err;

	sa_set_str(&laddr, "0.0.0.0", HTTP_PORT);

	err = http_listen(&httpsock, &laddr,
			   http_req_handler, NULL);
	if (err)
		return err;

	info("demo: listening on %J\n", &laddr);

	return 0;
}


int demo_close(void)
{
	sess = mem_deref(sess);
	conn_pending = mem_deref(conn_pending);
	httpsock = mem_deref(httpsock);

	return 0;
}
