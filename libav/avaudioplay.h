/*
* avaudio.h
*/

#ifndef __AV_AUDIO_H__
#define __AV_AUDIO_H__

#include "globals.h"
#include "avqueue.h"
//#include "avplay.h"

struct avplay;

typedef struct _avaudioplay {
	AVCodecContext *m_audio_ctx;

	/* 重采样音频指针.	*/
	struct SwrContext *m_swr_ctx;
	ReSampleContext *m_resample_ctx;

	/* 最后一个解码帧的pts, 解码帧缓冲大小为2, 也就是当前播放帧的下一帧.	*/
	double m_audio_clock;

	/* 当前音频播放buffer大小.	*/
	uint32_t m_audio_buf_size;

	/* 当前音频已经播放buffer的位置.	*/
	uint32_t m_audio_buf_index;
	int32_t m_audio_write_buf_size;
	double m_audio_current_pts_drift;
	double m_audio_current_pts_last;

	pthread_t m_audio_render_thrd;
	pthread_t m_audio_dec_thrd;

	struct avplay* m_play;

	/* 停止标志.	*/
	int m_abort;
} avaudioplay;

avaudioplay* avaudioplay_create(avplay* play, AVCodecContext *ctx);

int avaudioplay_start(avaudioplay* audio);

int avaudioplay_stop(avaudioplay* audio);

void avaudioplay_destroy(avaudioplay* audio);

/* 时钟函数. */
double avaudioplay_clock(avaudioplay* audio);

#endif /* AVPLAY_H_ */
