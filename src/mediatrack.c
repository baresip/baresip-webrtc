/**
 * @file src/mediatrack.c  RTC Media Track
 *
 * Copyright (C) 2021 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


static void destructor(void *data)
{
	struct media_track *media = data;

	list_unlink(&media->le);
}


struct media_track *media_track_add(struct list *lst,
				    struct peer_connection *pc,
				    enum media_kind kind)
{
	struct media_track *media;

	media = mem_zalloc(sizeof(*media), destructor);
	if (!media)
		return NULL;

	media->kind = kind;
	media->pc = pc;

	list_append(lst, &media->le, media);

	return media;
}


int mediatrack_start_audio(struct media_track *media,
			   struct list *ausrcl, struct list *aufiltl)
{
	const struct sdp_format *fmt;
	struct audio *au;
	int err = 0;

	if (!media)
		return EINVAL;

	au = media->u.au;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("mediatrack: start_audio: ice or dtls not ready\n");
		return EPROTO;
	}

	info("mediatrack: start audio\n");

	fmt = sdp_media_rformat(stream_sdpmedia(audio_strm(au)), NULL);
	if (fmt) {
		struct aucodec *ac = fmt->data;

		err = audio_encoder_set(au, ac, fmt->pt, fmt->params);
		if (err) {
			warning("mediatrack: start:"
				" audio_encoder_set error: %m\n", err);
			return err;
		}

		err = audio_start_source(au, ausrcl, aufiltl);
		if (err) {
			warning("mediatrack: start:"
				" audio_start_source error: %m\n", err);
			return err;
		}
	}
	else {
		info("mediatrack: audio stream is disabled..\n");
	}

	return 0;
}


int mediatrack_start_video(struct media_track *media)
{
	const struct sdp_format *fmt;
	struct video *vid;
	int err = 0;

	if (!media)
		return EINVAL;

	vid = media->u.vid;

	if (!media->ice_conn || !media->dtls_ok) {
		warning("mediatrack: start_video: ice or dtls not ready\n");
		return EPROTO;
	}

	info("mediatrack: start video\n");

	fmt = sdp_media_rformat(stream_sdpmedia(video_strm(vid)), NULL);
	if (fmt) {
		struct vidcodec *vc = fmt->data;

		err  = video_encoder_set(vid, vc, fmt->pt, fmt->params);
		if (err) {
			warning("mediatrack: start:"
				" video_encoder_set error: %m\n", err);
			return err;
		}

		err = video_start_source(vid);
		if (err) {
			warning("mediatrack: start:"
				" video_start error: %m\n", err);
			return err;
		}
	}
	else {
		info("mediatrack: video stream is disabled..\n");
	}

	return 0;
}


void mediatrack_stop(struct media_track *media)
{
	if (!media)
		return;

	switch (media->kind) {

	case MEDIA_KIND_AUDIO:
		audio_stop(media->u.au);
		break;

	case MEDIA_KIND_VIDEO:
		video_stop(media->u.vid);
		break;
	}
}


struct stream *media_get_stream(const struct media_track *media)
{
	if (!media)
		return NULL;

	switch (media->kind) {

	case MEDIA_KIND_AUDIO: return audio_strm(media->u.au);
	case MEDIA_KIND_VIDEO: return video_strm(media->u.vid);
	default:               return NULL;
	}
}


const char *media_kind_name(enum media_kind kind)
{
	switch (kind) {

	case MEDIA_KIND_AUDIO: return "audio";
	case MEDIA_KIND_VIDEO: return "video";
	default: return "???";
	}
}
