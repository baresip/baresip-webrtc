/**
 * @file demo.c  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


/*
  TODO:

  ok - add support for video
     - add support for multiple sessions
     - convert HTTP content to JSON?
     - add RTCPeerConnection.signalingState ?
 */

enum {HTTP_PORT = 9000};


static struct stun_uri *stun_srv;

static struct http_sock *httpsock;
static struct http_sock *httpssock;
static struct http_conn *conn_pending;
static struct rtcsession *sess;
static const struct mnat *mnat;
static const struct menc *menc;


static struct {
	const char *stun_user;
	const char *stun_pass;
} g;


static void reply_fmt(struct http_conn *conn, const char *ctype,
		      const char *fmt, ...)
{
	char *buf = NULL;
	va_list ap;
	int err;

	va_start(ap, fmt);
	err = re_vsdprintf(&buf, fmt, ap);
	va_end(ap);

	if (err)
		return;

	info("demo: reply: %s\n", ctype);

	http_reply(conn, 200, "OK",
		   "Content-Type: %s\r\n"
		   "Content-Length: %zu\r\n"
		   "Access-Control-Allow-Origin: *\r\n"
		   "\r\n"
		   "%s",
		   ctype, str_len(buf), buf);

	mem_deref(buf);
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
static int reply_descr(const char *type, struct mbuf *mb_sdp)
{
	struct odict *od = NULL;
	int err;

	err = session_description_encode(&od, type, mb_sdp);
	if (err)
		goto out;

	reply_fmt(conn_pending, "application/json",
		  "%H", json_encode_odict, od);

 out:
	mem_deref(od);

	return err;
}


static void session_gather_handler(void *arg)
{
	struct mbuf *mb_sdp = NULL;
	const char *type;
	bool send_offer;
	int err;
	(void)arg;

	send_offer = !rtcsession_got_offer(sess);
	type = send_offer ? "offer" : "answer";

	info("demo: session gathered -- send %s\n", type);

	err = rtcsession_encode_descr(sess, &mb_sdp, send_offer);
	if (err)
		goto out;

	err = reply_descr(type, mb_sdp);
	if (err)
		goto out;

	if (!send_offer) {

		err = rtcsession_start_ice(sess);
		if (err) {
			warning("demo: failed to start ice (%m)\n", err);
			goto out;
		}
	}

 out:
	mem_deref(mb_sdp);
}


static void session_estab_handler(bool audio, struct media_track *media,
				  void *arg)
{
	int err;

	(void)arg;

	info("demo: stream established: '%s'\n",
	     audio ? "audio" : "video");

	if (audio) {
		err = rtcsession_start_audio(sess, media);
		if (err) {
			warning("demo: could not start audio (%m)\n", err);
		}
	}
	else {
		err = rtcsession_start_video(sess, media);
		if (err) {
			warning("demo: could not start video (%m)\n", err);
		}
	}
}


static void session_close_handler(int err, void *arg)
{
	(void)arg;

	warning("demo: session closed (%m)\n", err);

	/* todo: notify client that session was closed */
	sess = mem_deref(sess);
}


/* RemoteDescription */
static int create_session(struct mbuf *offer)
{
	struct sa laddr;
	const struct config *config = conf_config();
	int err;

	info("demo: create session (offer=%s)\n", offer ? "yes" : "no");

	sa_set_str(&laddr, "127.0.0.1", 0);

	if (sess) {
		err = EBUSY;
		goto out;
	}

	/* create a new session object, send SDP to it */
	err = rtcsession_create(&sess, config, &laddr,
				offer, mnat, menc,
				stun_srv,
				g.stun_user, g.stun_pass,
				session_gather_handler,
				session_estab_handler,
				session_close_handler, NULL);
	if (err) {
		warning("demo: session alloc failed (%m)\n", err);
		goto out;
	}

	err = rtcsession_add_audio(sess, config, baresip_aucodecl());
	if (err) {
		warning("demo: add_audio failed (%m)\n", err);
		goto out;
	}

	err = rtcsession_add_video(sess, config, baresip_vidcodecl());
	if (err) {
		warning("demo: add_video failed (%m)\n", err);
		goto out;
	}

	if (offer) {
		err = rtcsession_decode_descr(sess, offer, true);
		if (err) {
			warning("demo: decode offer failed (%m)\n", err);
			goto out;
		}
	}

 out:
	if (err)
		sess = mem_deref(sess);

	return err;
}


static int handle_put_sdp(const struct http_msg *msg)
{
	struct session_description sd = {"",NULL};
	struct mbuf *offer = NULL;
	int err = 0;

	info("demo: handle PUT sdp: content is '%r/%r'\n",
	     &msg->ctyp.type, &msg->ctyp.subtype);

	if (msg->clen) {

		if (msg_ctype_cmp(&msg->ctyp, "application", "json")) {

			err = session_description_decode(&sd, msg->mb);
			if (err)
				goto out;

			if (0 == str_casecmp(sd.type, "offer")) {

				offer = sd.sdp;
			}
			else if (0 == str_casecmp(sd.type, "answer")) {

				err = rtcsession_decode_descr(sess, sd.sdp,
							      false);
				if (err) {
					warning("decode error (%m)\n", err);
				}

				err = rtcsession_start_ice(sess);
				if (err) {
					warning("demo: failed to start ice"
						" (%m)\n", err);
					goto out;
				}
			}
			else {
				warning("invalid session description type:"
					" %s\n",
					sd.type);
				err = EPROTO;
				goto out;
			}
		}
		else {
			warning("unknown content-type: %r/%r\n",
				&msg->ctyp.type, &msg->ctyp.subtype);
			err = EPROTO;
			goto out;
		}

		err = create_session(offer);
		if (err)
			goto out;
	}

out:
	session_description_reset(&sd);

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

		/* TODO: generate a unique session id */

		/* sync reply */
		http_reply(conn, 200, "OK",
			   "Content-Length: 0\r\n"
			   "Access-Control-Allow-Origin: *\r\n"
			   "\r\n");
	}
	else if (0 == pl_strcasecmp(&msg->met, "PUT") &&
		 0 == pl_strcasecmp(&msg->path, "/sdp")) {

		err = handle_put_sdp(msg);
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
		warning("not found: %r %r\n", &msg->met, &msg->path);
		http_ereply(conn, 404, "Not Found");
	}

 out:
	if (err)
		http_ereply(conn, 500, "Server Error");

	mem_deref(buf);
	mem_deref(mb);
}


int demo_init(const char *ice_server,
	      const char *stun_user, const char *stun_pass)
{
	struct pl srv;
	struct sa laddr;
	int err;

	if (ice_server) {
		pl_set_str(&srv, ice_server);

		err = stunuri_decode(&stun_srv, &srv);
		if (err) {
			warning("demo: invalid iceserver '%r' (%m)\n",
				&srv, err);
			return err;
		}
	}

	g.stun_user = stun_user;
	g.stun_pass = stun_pass;

	mnat = mnat_find(baresip_mnatl(), "ice");
	if (!mnat) {
		warning("demo: medianat 'ice' not found\n");
		return ENOENT;
	}

	menc = menc_find(baresip_mencl(), "dtls_srtp");
	if (!menc) {
		warning("demo: mediaenc 'dtls-srtp' not found\n");
		return ENOENT;
	}

	sa_set_str(&laddr, "0.0.0.0", HTTP_PORT);

	err = http_listen(&httpsock, &laddr,
			   http_req_handler, NULL);
	if (err)
		return err;

	info("demo: listening on HTTP %J\n", &laddr);

	sa_set_port(&laddr, sa_port(&laddr) + 1);

	err = https_listen(&httpssock, &laddr, "./share/cert.pem",
			   http_req_handler, NULL);
	if (err)
		return err;

	info("demo: listening on HTTPS %J\n", &laddr);

	return 0;
}


int demo_close(void)
{
	sess = mem_deref(sess);
	conn_pending = mem_deref(conn_pending);
	httpssock = mem_deref(httpssock);
	httpsock = mem_deref(httpsock);
	stun_srv = mem_deref(stun_srv);

	return 0;
}
