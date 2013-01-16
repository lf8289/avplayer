/*
* avvideoplay.h
*/

#ifndef __AV_VIDEO_PLAY_H__
#define __AV_VIDEO_PLAY_H__

#include "globals.h"
#include "avqueue.h"
//#include "avplay.h"

/* 计算视频实时帧率和实时码率的时间单元. */
#define MAX_CALC_SEC 5

struct avplay;

typedef struct _avvideoplay {
	AVCodecContext *m_video_ctx;

	/* 重采样音频指针.	*/
	struct SwsContext *m_swsctx;

	/* 最后一个解码帧的pts, 解码帧缓冲大小为2, 也就是当前播放帧的下一帧.	*/
	double m_video_clock;

	/*
	* 用于计算视频播放时间
	* 即:  m_video_current_pts_drift = m_video_current_pts - time();
	*      m_video_current_pts是当前播放帧的pts时间, 所以在pts向前推进
	*      的同时, time也同样在向前推进, 所以pts_drift基本保存在一个
	*      time_base范围内浮动.
	* 播放时间 = m_video_current_pts_drift - time()
	*/
	double m_video_current_pts_drift;
	double m_video_current_pts;

	/* 以下变量用于计算音视频同步.	*/
	double m_frame_timer;
	double m_frame_last_pts;
	double m_frame_last_duration;
	double m_frame_last_delay;
	double m_frame_last_filter_delay;
	double m_frame_last_dropped_pts;
	double m_frame_last_returned_time;
	int64_t m_frame_last_dropped_pos;
	int64_t m_video_current_pos;
	int m_drop_frame_num;

	/* 帧率. */
	int m_enable_calc_frame_rate;
	int m_real_frame_rate;
	int m_frame_num[MAX_CALC_SEC]; /* 记录5秒内的帧数. */
	int m_last_fr_time;
	int m_fr_index;

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
