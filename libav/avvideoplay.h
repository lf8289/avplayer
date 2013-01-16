/*
* avvideoplay.h
*/

#ifndef __AV_VIDEO_PLAY_H__
#define __AV_VIDEO_PLAY_H__

#include "globals.h"
#include "avqueue.h"
//#include "avplay.h"

struct avplay;

typedef struct _avvideoplay {
	AVCodecContext *m_video_ctx;
	pthread_t m_video_dec_thrd;
	pthread_t m_video_render_thrd;

	struct avplay* m_play;

	/* 停止标志.	*/
	int m_abort;
} avvideoplay;

avvideoplay* avvideoplay_create(avplay* play, AVCodecContext* ctx);

int avvideoplay_start(avvideoplay* video);

int avvideoplay_stop(avvideoplay* video);

void avvideoplay_destroy(avvideoplay* video);

/* 时钟函数. */
double avvideoplay_clock(avvideoplay* video);


#endif /*__AV_VIDEO_PLAY_H__*/
