/*
* avvideoplay.h
*/

#ifndef __AV_VIDEO_PLAY_H__
#define __AV_VIDEO_PLAY_H__

#include "globals.h"
#include "avqueue.h"
#include "avplay.h"

	void* video_dec_thrd(void *param);

	void* video_render_thrd(void *param);

	/* 视频帧复制. */
	void video_copy(avplay *play, AVFrame *dst, AVFrame *src);

	/* 更新视频pts. */
	void update_video_pts(avplay *play, double pts, int64_t pos);

	double video_clock(avplay *play);


#endif /*__AV_VIDEO_PLAY_H__*/
