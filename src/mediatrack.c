
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


const char *media_kind_name(enum media_kind kind)
{
	switch (kind) {

	case MEDIA_KIND_AUDIO: return "audio";
	case MEDIA_KIND_VIDEO: return "video";
	default: return "???";
	}
}
