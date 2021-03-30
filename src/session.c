/**
 * @file src/session.c  RTC Session
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


struct rtcsession {
	struct stream_param stream_prm;
	struct list streaml;
	struct list medial;
	struct sdp_session *sdp;
	const struct mnat *mnat;
	struct mnat_sess *mnats;
	const struct menc *menc;
	struct menc_sess *mencs;
	struct media_ctx *ctx;
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


static struct stream *media_get_stream(const struct media_track *media)
{
	switch (media->kind) {

	case MEDIA_KIND_AUDIO: return audio_strm(media->u.au);
	case MEDIA_KIND_VIDEO: return video_strm(media->u.vid);
	default:               return NULL;
	}
}


static void destructor(void *data)
{
	struct rtcsession *sess = data;
	struct le *le;
	size_t i;

	info("rtcsession: destroyed\n");

	info("*** RTCSession summary ***\n");

	info("steps:\n");
	info(".. gather:   %d\n", sess->gather_ok);
	info(".. sdp:      %d\n", sess->sdp_ok);
	info("\n");

	i=0;
	for (le = sess->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		if (!media->u.p)
			continue;

		info(".. #%zu '%s'\n",
		     i, stream_name(media_get_stream(media)));
		info(".. ice_conn: %d\n", media->ice_conn);
		info(".. dtls:     %d\n", media->dtls_ok);
		info(".. rtp:      %d\n", media->rtp);
		info(".. rtcp:     %d\n", media->rtcp);
		info("\n");

		++i;
	}

	info("\n");

	for (le = sess->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		if (!media->u.p)
			continue;

		switch (media->kind) {

		case MEDIA_KIND_AUDIO:
			debug("%H\n", audio_debug, media->u.au);
			break;

		case MEDIA_KIND_VIDEO:
			debug("%H\n", video_debug, media->u.vid);
			break;
		}
	}

	le = sess->medial.head;
	while (le) {
		struct media_track *media = le->data;

		le = le->next;

		switch (media->kind) {

		case MEDIA_KIND_AUDIO:
			audio_stop(media->u.au);
			break;

		case MEDIA_KIND_VIDEO:
			video_stop(media->u.vid, NULL);
			break;
		}

		mem_deref(media->u.p);
		mem_deref(media);
	}

	mem_deref(sess->sdp);
	mem_deref(sess->mnats);
	mem_deref(sess->mencs);
}


static void media_destructor(void *data)
{
	struct media_track *media = data;

	list_unlink(&media->le);
}


static struct media_track *media_add(struct rtcsession *sess,
				     enum media_kind kind)
{
	struct media_track *media;

	media = mem_zalloc(sizeof(*media), media_destructor);

	media->kind = kind;
	media->sess = sess;

	list_append(&sess->medial, &media->le, media);

	return media;
}


static struct media_track *lookup_media(struct rtcsession *sess,
				  struct stream *strm)
{
	struct le *le;

	for (le = sess->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

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
	(void)end;

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
	struct media_track *media;

	media = lookup_media(sess, strm);

	info("rtcsession: mediaenc event '%s' (%s)\n",
	     menc_event_name(event), prm);

	switch (event) {

	case MENC_EVENT_SECURE:
		media->dtls_ok = true;

		stream_set_secure(strm, true);
		stream_start(strm);

		if (strstr(prm, "audio")) {

			if (sess->estabh)
				sess->estabh(media, sess->arg);
		}
		else if (strstr(prm, "video")) {

			if (sess->estabh)
				sess->estabh(media, sess->arg);
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
	struct media_track *media = arg;
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
	struct media_track *media = arg;

	info("rtcsession: rtp established (%s)\n", stream_name(strm));

	media->rtp = true;
}


static void rtcp_handler(struct stream *strm,
			 struct rtcp_msg *msg, void *arg)
{
	struct media_track *media = arg;
	(void)strm;
	(void)msg;

	media->rtcp = true;
}


static void stream_error_handler(struct stream *strm, int err, void *arg)
{
	struct media_track *media = arg;

	warning("rtcsession: '%s' stream error (%m)\n",
		stream_name(strm), err);

	session_close(media->sess, err);
}


int rtcsession_create(struct rtcsession **sessp, const struct config *cfg,
		      const struct sa *laddr,
		      struct mbuf *offer,
		      const struct mnat *mnat, const struct menc *menc,
		      struct stun_uri *stun_srv,
		      const char *stun_user, const char *stun_pass,
		      rtcsession_gather_h *gatherh,
		      rtcsession_estab_h *estabh,
		      rtcsession_close_h *closeh, void *arg)
{
	struct rtcsession *sess;
	bool got_offer = offer != NULL;
	int err;

	if (!sessp || !cfg || !laddr)
		return EINVAL;

	if (!mnat || !menc)
		return EINVAL;

	info("rtcsession: create session, laddr = %j\n", laddr);

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

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
				  stun_srv,
				  stun_user, stun_pass,
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
	struct media_track *media;
	struct stream *strm;
	int err;

	if (!sess || !cfg || !aucodecl)
		return EINVAL;

	info("rtcsession: add audio (codecs=%u)\n", list_count(aucodecl));

	media = media_add(sess, MEDIA_KIND_AUDIO);

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

	audio_set_media_context(media->u.au, &sess->ctx);

	strm = audio_strm(media->u.au);

	stream_set_session_handlers(strm, mnatconn_handler,
				    rtpestab_handler,
				    rtcp_handler,
				    stream_error_handler, media);

	return 0;
}


int rtcsession_add_video(struct rtcsession *sess,
			 const struct config *cfg,
			 struct list *vidcodecl)
{
	struct media_track *media;
	struct stream *strm;
	int err;

	if (!sess || !cfg || !vidcodecl)
		return EINVAL;

	info("rtcsession: add video (codecs=%u)\n", list_count(vidcodecl));

	media = media_add(sess, MEDIA_KIND_VIDEO);

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

	return 0;
}


int rtcsession_decode_descr(struct rtcsession *sess, struct mbuf *sdp,
			    bool offer)
{
	struct le *le;
	int err;

	if (!sess || !sdp)
		return EINVAL;

	info("rtcsession: decode %s\n", offer ? "offer" : "answer");

#if 1
	re_printf("- - %s - -\n", offer ? "offer" : "answer");
	re_printf("%b\n", (sdp)->buf, (sdp)->end);
	re_printf("- - - - - - -\n");
#endif

	err = sdp_decode(sess->sdp, sdp, offer);
	if (err) {
		warning("rtcsession: sdp decode failed (%m)\n", err);
		return err;
	}

	/* must be done after sdp_decode() */
	for (le = sess->medial.head; le; le = le->next) {
		struct media_track *media = le->data;

		if (!media->u.p)
			continue;

		switch (media->kind) {

		case MEDIA_KIND_VIDEO:
			video_sdp_attr_decode(media->u.vid);
			break;

		default:
			break;
		}
	}

	/* must be done after sdp_decode() */
	for (le = sess->streaml.head; le; le = le->next) {
		struct stream *strm = le->data;

		stream_update(strm);
	}

	return 0;
}


int rtcsession_encode_descr(struct rtcsession *sess, struct mbuf **mb,
			    bool offer)
{
	int err;

	if (!sess)
		return EINVAL;

	if (!sess->gather_ok) {
		warning("rtcsession: sdp: ice not gathered\n");
		return EPROTO;
	}

	info("rtcsession: encode %s\n", offer ? "offer" : "answer");

	err = sdp_encode(mb, sess->sdp, offer);
	if (err)
		return err;

#if 1
	re_printf("- - %s - -\n", offer ? "offer" : "answer");
	re_printf("%b\n", (*mb)->buf, (*mb)->end);
	re_printf("- - - - - - -\n");
#endif

	sess->sdp_ok = true;

	return 0;
}


int rtcsession_start_ice(struct rtcsession *sess)
{
	int err;

	if (!sess)
		return EINVAL;

	info(".. rtcsession: start ice\n");

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


int rtcsession_start_audio(struct rtcsession *sess, struct media_track *media)
{
	const struct sdp_format *sc;
	struct audio *au;
	int err = 0;

	if (!sess)
		return EINVAL;

	au = media->u.au;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("rtcsession: start_audio: ice or dtls not ready\n");
		return EPROTO;
	}

	info("rtcsession: start audio\n");

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

		err = audio_start_source(au, baresip_ausrcl(),
					 baresip_aufiltl());
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


int rtcsession_start_video(struct rtcsession *sess, struct media_track *media)
{
	const struct sdp_format *sc;
	struct video *vid;
	int err = 0;

	if (!sess)
		return EINVAL;

	vid = media->u.vid;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("rtcsession: start_video: ice or dtls not ready\n");
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

		err = video_start_source(vid, &sess->ctx);
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


/* todo: replace with signalingstate */
bool rtcsession_got_offer(const struct rtcsession *sess)
{
	return sess ? sess->got_offer : false;
}
