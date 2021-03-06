/*
* avqueue.h
*
*/

#ifndef __AV_QUEUE_H__
#define __AV_QUEUE_H__

#include "globals.h"

#ifdef _MSC_VER
#	include <windows.h>
#endif
#include <pthread.h>

//#include <libavformat/avformat.h>

#include <assert.h>



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

static inline int queue_size(av_queue *q)
{
    return q->m_size;
}

static inline int queue_type(av_queue *q)
{
    return q->m_type;
}

static inline void queue_set_type(av_queue *q, int type)
{
    q->m_type = type;
}

void chk_queue(av_queue *q, int size);

void queue_stop(av_queue *q);

#endif /*__AV_QUEUE_H__*/
