/*
* avplay.h
* ~~~~~~~~
*
* Copyright (c) 2011 Jack (jack.wgm@gmail.com)
*
*/

#ifndef AVQUEUE_H_
#define AVQUEUE_H_

#ifdef _MSC_VER
#	include <windows.h>
#	define inline
#	define __CRT__NO_INLINE
#	ifdef API_EXPORTS
#		define EXPORT_API __declspec(dllexport)
#	else
#		define EXPORT_API __declspec(dllimport)
#	endif
#else
#	define EXPORT_API
#endif

#include <pthread.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <assert.h>
#include "globals.h"

#ifdef  __cplusplus
extern "C" {
#endif


	/* 队列类型.	*/
#define QUEUE_PACKET			0
#define QUEUE_AVFRAME			1

	typedef struct AVFrameList
	{
		AVFrame pkt;
		struct AVFrameList *next;
	} AVFrameList;

	/* 队列.	*/
	typedef struct _av_queue
	{
		void *m_first_pkt, *m_last_pkt;
		int m_size; /* 队列大小.	*/
		int m_type; /* 队列类型.	*/
		int abort_request;
		pthread_mutex_t m_mutex;
		pthread_cond_t m_cond;
	} av_queue;

	/* 队列操作. */
	void queue_init(av_queue *q);
	void queue_flush(av_queue *q);
	void queue_end(av_queue *q);

	/* 入队出队列操作. */
	int get_queue(av_queue *q, void *p);
	int put_queue(av_queue *q, void *p);

	void chk_queue(av_queue *q, int size);

	void queue_stop(av_queue *q);

#ifdef  __cplusplus
}
#endif

#endif /* AVQUEUE_H_ */
