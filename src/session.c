/**
 * @file src/session.c  RTC Session
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


enum media_type {
	MEDIA_TYPE_AUDIO,
	MEDIA_TYPE_VIDEO,
};


/* one-to-one mapping with stream */
struct media {
	enum media_type type;
	union {
		struct audio *au;
		struct video *vid;
		void *p;
	} u;

	struct rtcsession *sess;  /* parent */
	unsigned ix;
	bool ice_conn;
	bool dtls_ok;
	bool rtp;
	bool rtcp;
};


struct rtcsession {
	struct stream_param stream_prm;
	struct list streaml;
	struct sdp_session *sdp;
	struct media mediav[4];
	size_t mediac;
	const struct mnat *mnat;
	struct mnat_sess *mnats;
	const struct menc *menc;
	struct menc_sess *mencs;
	char cname[16];
	bool got_offer;
	rtcsession_gather_h *gatherh;
	rtcsession_estab_h *estabh;
	rtcsession_close_h *closeh;
	void *arg;

	/* steps: */
	bool gather_ok;
	bool sdp_ok;
};


static struct stream *media_get_stream(const struct media *media)
{
	switch (media->type) {

	case MEDIA_TYPE_AUDIO: return audio_strm(media->u.au);
	case MEDIA_TYPE_VIDEO: return video_strm(media->u.vid);
	default:               return NULL;
	}
}


static void destructor(void *data)
{
	struct rtcsession *sess = data;
	size_t i;

	info("rtcsession: destroyed\n");

	info("*** RTCSession summary ***\n");

	info("steps:\n");
	info(".. gather:   %d\n", sess->gather_ok);
	info(".. sdp:      %d\n", sess->sdp_ok);
	info("\n");

	for (i=0; i<ARRAY_SIZE(sess->mediav); i++) {
		struct media *media = &sess->mediav[i];

		if (!media->u.p)
			continue;

		info(".. #%u '%s'\n",
		     media->ix, stream_name(media_get_stream(media)));
		info(".. ice_conn: %d\n", media->ice_conn);
		info(".. dtls:     %d\n", media->dtls_ok);
		info(".. rtp:      %d\n", media->rtp);
		info(".. rtcp:     %d\n", media->rtcp);
		info("\n");
	}

	info("\n");

	for (i=0; i<ARRAY_SIZE(sess->mediav); i++) {
		struct media *media = &sess->mediav[i];

		switch (media->type) {

		case MEDIA_TYPE_AUDIO:
			audio_stop(media->u.au);
			break;

		case MEDIA_TYPE_VIDEO:
			video_stop(media->u.vid);
			break;
		}

		mem_deref(media->u.p);
	}

	mem_deref(sess->sdp);
	mem_deref(sess->mnats);
	mem_deref(sess->mencs);
}


static struct media *lookup_media(struct rtcsession *sess,
				  struct stream *strm)
{
	size_t i;

	for (i=0; i<ARRAY_SIZE(sess->mediav); i++) {

		struct media *media = &sess->mediav[i];

		if (strm == media_get_stream(media))
			return media;
	}

	return NULL;
}


static void session_close(struct rtcsession *sess, int err)
{
	rtcsession_close_h *closeh = sess->closeh;

	sess->closeh = NULL;

	if (closeh)
		closeh(err, sess->arg);
}


static void audio_event_handler(int key, bool end, void *arg)
{
	struct rtcsession *sess = arg;
	(void)sess;

	info("rtcsession: recv DTMF event: key=%d ('%c')\n", key, key);
}


static void audio_err_handler(int err, const char *str, void *arg)
{
	struct rtcsession *sess = arg;

	warning("rtcsession: audio error: %m (%s)\n", err, str);

	session_close(sess, err);
}


static void video_err_handler(int err, const char *str, void *arg)
{
	struct rtcsession *sess = arg;

	warning("rtcsession: video error: %m (%s)\n", err, str);

	session_close(sess, err);
}


static void mnat_estab_handler(int err, uint16_t scode, const char *reason,
			       void *arg)
{
	struct rtcsession *sess = arg;

	if (err) {
		warning("rtcsession: medianat failed: %m\n", err);
		session_close(sess, err);
		return;
	}
	else if (scode) {
		warning("rtcsession: medianat failed: %u %s\n", scode, reason);
		session_close(sess, EPROTO);
		return;
	}

	info("rtcsession: medianat established/gathered (all streams)\n");

	sess->gather_ok = true;

	if (sess->gatherh)
		sess->gatherh(sess->arg);
}


static void menc_event_handler(enum menc_event event,
			       const char *prm, struct stream *strm,
			       void *arg)
{
	struct rtcsession *sess = arg;
	struct media *media;

	media = lookup_media(sess, strm);

	info("rtcsession: mediaenc event '%s' (%s) ix=%u\n",
	     menc_event_name(event), prm, media->ix);

	switch (event) {

	case MENC_EVENT_SECURE:
		media->dtls_ok = true;

		stream_set_secure(strm, true);
		stream_start(strm);

		if (strstr(prm, "audio")) {

			if (sess->estabh)
				sess->estabh(true, media->ix, sess->arg);
		}
		else if (strstr(prm, "video")) {


			if (sess->estabh)
				sess->estabh(false, media->ix, sess->arg);
		}
		else {
			info("rtcsession: mediaenc: no match for stream"
			     " (%s)\n", prm);
		}
		break;

	default:
		break;
	}
}


static void menc_error_handler(int err, void *arg)
{
	struct rtcsession *sess = arg;

	warning("rtcsession: mediaenc error: %m\n", err);

	if (sess->closeh)
		sess->closeh(err, sess->arg);
}


static void mnatconn_handler(struct stream *strm, void *arg)
{
	struct media *media = arg;
	int err;

	info("rtcsession: ice connected (%s)\n", stream_name(strm));

	media->ice_conn = true;

	err = stream_start_mediaenc(strm);
	if (err) {
		session_close(media->sess, err);
	}
}


static void rtpestab_handler(struct stream *strm, void *arg)
{
	struct media *media = arg;

	info("rtcsession: rtp established (%s)\n", stream_name(strm));

	media->rtp = true;
}


static void rtcp_handler(struct stream *strm,
			 struct rtcp_msg *msg, void *arg)
{
	struct media *media = arg;

	media->rtcp = true;
}


static void stream_error_handler(struct stream *strm, int err, void *arg)
{
	struct media *media = arg;

	warning("rtcsession: '%s' stream error (%m)\n",
		stream_name(strm), err);

	session_close(media->sess, err);
}


int rtcsession_create(struct rtcsession **sessp, const struct config *cfg,
		      const struct rtcsession_param *prm,
		      const struct sa *laddr,
		      struct mbuf *offer,
		      const struct mnat *mnat, const struct menc *menc,
		      const char *stun_server, uint16_t stun_port,
		      rtcsession_gather_h *gatherh,
		      rtcsession_estab_h *estabh,
		      rtcsession_close_h *closeh, void *arg)
{
	struct rtcsession *sess;
	bool got_offer = offer != NULL;
	size_t i;
	int err;

	if (!sessp || !cfg || !prm || !laddr)
		return EINVAL;

	if (!mnat || !menc)
		return EINVAL;

	info("rtcsession: create session, laddr = %j\n", laddr);

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	for (i=0; i<ARRAY_SIZE(sess->mediav); i++) {

		sess->mediav[i].sess = sess;
	}

	/* RFC 7022 */
	rand_str(sess->cname, sizeof(sess->cname));

	sess->stream_prm.use_rtp = true;
	sess->stream_prm.af      = sa_af(laddr);
	sess->stream_prm.cname   = sess->cname;

	err = sdp_session_alloc(&sess->sdp, laddr);
	if (err)
		goto out;

	if (mnat->sessh) {

		info("rtcsession: using mnat '%s'\n", mnat->id);

		sess->mnat = mnat;

		err = mnat->sessh(&sess->mnats, mnat,
				  net_dnsc(baresip_network()),
				  sa_af(laddr),
				  stun_server, stun_port,
				  NULL, NULL,
				  sess->sdp, !got_offer,
				  mnat_estab_handler, sess);
		if (err) {
			warning("rtcsession: medianat session: %m\n", err);
			goto out;
		}
	}

	if (menc->sessh) {
		info("rtcsession: using menc '%s'\n", menc->id);

		sess->menc = menc;

		err = menc->sessh(&sess->mencs, sess->sdp, !got_offer,
				  menc_event_handler,
				  menc_error_handler, sess);
		if (err) {
			warning("rtcsession: mediaenc session: %m\n", err);
			goto out;
		}
	}

	sess->got_offer = got_offer;

	sess->gatherh = gatherh;
	sess->estabh = estabh;
	sess->closeh = closeh;
	sess->arg = arg;

 out:
	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


/* todo: add per-audio configuration */
int rtcsession_add_audio(struct rtcsession *sess,
			 const struct config *cfg,
			 struct list *aucodecl)
{
	struct media *media;
	struct stream *strm;
	int err;

	if (!sess || !cfg || !aucodecl)
		return EINVAL;

	if (sess->mediac >= ARRAY_SIZE(sess->mediav))
		return EOVERFLOW;

	info("rtcsession: add audio (ix=%u)\n", sess->mediac);

	media = &sess->mediav[sess->mediac];

	media->type = MEDIA_TYPE_AUDIO;
	media->ix = (unsigned)sess->mediac;

	err = audio_alloc(&media->u.au, &sess->streaml,
			  &sess->stream_prm, cfg,
			  NULL, sess->sdp, 0,
			  sess->mnat, sess->mnats,
			  sess->menc, sess->mencs,
			  20, aucodecl, !sess->got_offer,
			  audio_event_handler, NULL,
			  audio_err_handler, sess);
	if (err) {
		warning("rtcsession: audio alloc failed (%m)\n", err);
		return err;
	}

	strm = audio_strm(media->u.au);

	stream_set_session_handlers(strm, mnatconn_handler,
				    rtpestab_handler,
				    rtcp_handler,
				    stream_error_handler, media);

	++sess->mediac;

	return 0;
}


int rtcsession_add_video(struct rtcsession *sess,
			 const struct config *cfg,
			 struct list *vidcodecl)
{
	struct media *media;
	struct stream *strm;
	int err;

	if (!sess || !cfg || !vidcodecl)
		return EINVAL;

	if (sess->mediac >= ARRAY_SIZE(sess->mediav))
		return EOVERFLOW;

	info("rtcsession: add video (ix=%u)\n", sess->mediac);

	media = &sess->mediav[sess->mediac];

	media->type = MEDIA_TYPE_VIDEO;
	media->ix = (unsigned)sess->mediac;

	err = video_alloc(&media->u.vid, &sess->streaml,
			  &sess->stream_prm,
			  cfg,
			  sess->sdp, 0,
			  sess->mnat, sess->mnats,
			  sess->menc, sess->mencs,
			  NULL, vidcodecl,
			  NULL,
			  !sess->got_offer,
			  video_err_handler, sess);
	if (err) {
		warning("rtcsession: video alloc failed (%m)\n", err);
		return err;
	}

	strm = video_strm(media->u.vid);

	stream_set_session_handlers(strm, mnatconn_handler,
				    rtpestab_handler,
				    rtcp_handler,
				    stream_error_handler, media);

	++sess->mediac;

	return 0;
}


int rtcsession_decode_offer(struct rtcsession *sess, struct mbuf *offer)
{
	struct le *le;
	int err;

	if (!sess || !offer)
		return EINVAL;

	info("rtcsession: decode offer\n");

	err = sdp_decode(sess->sdp, offer, true);
	if (err) {
		warning("rtcsession: sdp decode failed (%m)\n", err);
		return err;
	}

	/* must be done after sdp_decode() */
	for (le = sess->streaml.head; le; le = le->next) {
		struct stream *strm = le->data;

		stream_update(strm);
	}

	return 0;
}


int rtcsession_encode_answer(struct rtcsession *sess, struct mbuf **mb)
{
	int err;

	if (!sess)
		return EINVAL;

	if (!sess->gather_ok) {
		warning("rtcsession: sdp: ice not gathered\n");
		return EPROTO;
	}

	info("rtcsession: encode answer\n");

	err = sdp_encode(mb, sess->sdp, false);
	if (err)
		return err;

	sess->sdp_ok = true;

	return 0;
}


int rtcsession_start_ice(struct rtcsession *sess)
{
	int err;

	if (!sess)
		return EINVAL;

	if (!sess->sdp_ok) {
		warning("rtcsession: ice: sdp not ready\n");
		return EPROTO;
	}

	if (sess->mnat->updateh && sess->mnats) {
		err = sess->mnat->updateh(sess->mnats);
		if (err) {
			warning("rtcsession: mnat update failed (%m)\n", err);
			return err;
		}
	}

	return 0;
}


int rtcsession_start_audio(struct rtcsession *sess, unsigned mediaix)
{
	const struct sdp_format *sc;
	struct audio *au;
	struct media *media;
	int err = 0;

	if (!sess)
		return EINVAL;

	if (mediaix >= ARRAY_SIZE(sess->mediav))
		return ENOENT;

	media = &sess->mediav[mediaix];
	au = media->u.au;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("rtcsession: start_audio: ice or dtls not ready\n");
		return EPROTO;
	}

	info("rtcsession: start audio (ix=%u)\n", mediaix);

	/* Audio Stream */
	sc = sdp_media_rformat(stream_sdpmedia(audio_strm(au)), NULL);
	if (sc) {
		struct aucodec *ac = sc->data;

		err  = audio_encoder_set(au, ac, sc->pt, sc->params);
		if (err) {
			warning("rtcsession: start:"
				" audio_encoder_set error: %m\n", err);
			return err;
		}

		err = audio_start(au);
		if (err) {
			warning("rtcsession: start:"
				" audio_start error: %m\n", err);
			return err;
		}
	}
	else {
		info("rtcsession: audio stream is disabled..\n");
	}

	return 0;
}


int rtcsession_start_video(struct rtcsession *sess, unsigned mediaix)
{
	const struct sdp_format *sc;
	struct video *vid;
	struct media *media;
	int err = 0;

	if (!sess)
		return EINVAL;

	if (mediaix >= ARRAY_SIZE(sess->mediav))
		return ENOENT;

	media = &sess->mediav[mediaix];
	vid = media->u.vid;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("rtcsession: start_video: ice or dtls not ready"
			" (ix=%u)\n", mediaix);
		return EPROTO;
	}

	info("rtcsession: start video\n");

	/* Video Stream */
	sc = sdp_media_rformat(stream_sdpmedia(video_strm(vid)), NULL);
	if (sc) {
		struct vidcodec *vc = sc->data;

		err  = video_encoder_set(vid, vc, sc->pt, sc->params);
		if (err) {
			warning("rtcsession: start:"
				" video_encoder_set error: %m\n", err);
			return err;
		}

		err = video_start(vid, NULL);
		if (err) {
			warning("rtcsession: start:"
				" video_start error: %m\n", err);
			return err;
		}
	}
	else {
		info("rtcsession: video stream is disabled..\n");
	}

	return 0;
}
