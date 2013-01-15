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
	pthread_t m_audio_render_thrd;
	pthread_t m_audio_dec_thrd;

	struct avplay* m_play;

	/* 停止标志.	*/
	int m_abort;
} avaudioplay;

avaudioplay* avaudioplay_create(avplay* play);

int avaudioplay_start(avaudioplay* audio);

int avaudioplay_stop(avaudioplay* audio);

void avaudioplay_destroy(avaudioplay* audio);

/* 时钟函数. */
double avaudioplay_clock(avaudioplay* audio);

#endif /* AVPLAY_H_ */
