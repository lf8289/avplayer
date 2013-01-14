/*
* avaudio.h
*/

#ifndef __AV_AUDIO_H__
#define __AV_AUDIO_H__

#include <pthread.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <assert.h>
#include "globals.h"
#include "avqueue.h"
#include "avlogger.h"

#ifdef  __cplusplus
extern "C" {
#endif

void* audio_dec_thrd(void *param);

/* 渲染线程. */
void* audio_render_thrd(void *param);

/* 视频帧复制. */
void audio_copy(avplay *play, AVFrame *dst, AVFrame *src);

/* 时钟函数. */
double audio_clock(avplay *play);

#ifdef  __cplusplus
}
#endif

#endif /* AVPLAY_H_ */
