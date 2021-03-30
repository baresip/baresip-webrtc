
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


static void media_destructor(void *data)
{
	struct media_track *media = data;

	list_unlink(&media->le);
}


struct media_track *media_track_add(struct list *lst, struct rtcsession *sess,
				    enum media_kind kind)
{
	struct media_track *media;

	media = mem_zalloc(sizeof(*media), media_destructor);
	if (!media)
		return NULL;

	media->kind = kind;
	media->sess = sess;

	list_append(lst, &media->le, media);

	return media;
}


int mediatrack_start_audio(struct media_track *media)
{
	const struct sdp_format *sc;
	struct audio *au;
	int err = 0;

	if (!media)
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


int mediatrack_start_video(struct media_track *media)
{
	const struct sdp_format *sc;
	struct video *vid;
	int err = 0;

	if (!media)
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

		err = video_start_source(vid, NULL);
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


const char *media_kind_name(enum media_kind kind)
{
	switch (kind) {

	case MEDIA_KIND_AUDIO: return "audio";
	case MEDIA_KIND_VIDEO: return "video";
	default: return "???";
	}
}
