/*
* avplay.h
* ~~~~~~~~
*
* Copyright (c) 2011 Jack (jack.wgm@gmail.com)
*
*/

#ifndef AVPLAY_H_
#define AVPLAY_H_

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

#include "globals.h"
#include "avqueue.h"
#include "avaudioplay.h"
#include "avvideoplay.h"




class VMPlayer;
typedef struct avplay {
    VMPlayer* player;
    ao_context *m_ao_ctx;
    vo_context *m_vo_ctx;
    source_context *m_source_ctx;
} avplay;

__EXTERN_C_BEGIN
/* 数据源结构分配和释放. */
EXPORT_API source_context* alloc_media_source(int type, const char *addition, int addition_len, int64_t size);
EXPORT_API void free_media_source(source_context *ctx);

/* 音频结构分配和释放. */
EXPORT_API ao_context* alloc_audio_render();
EXPORT_API void free_audio_render(ao_context *ctx);

/* 视频渲染结构分配和释放. */
EXPORT_API vo_context* alloc_video_render(void *user_data);
EXPORT_API void free_video_render(vo_context *ctx);



/*
* Assign a player structural context.
* @If the function succeeds, the return value is a pointer to the avplay,
* If the function fails, the return value is NULL.
*/
EXPORT_API avplay* alloc_avplay_context();

/*
* The release of the structural context of the player.
* @param ctx allocated by alloc_avplay_context.
* @This function does not return a value.
*/
EXPORT_API void free_avplay_context(avplay *ctx);

/*
* Initialize the player.
* @param play pointer to user-supplied avplayer (allocated by alloc_avplay_context).
* @param sc source_context use to read media data.
* @return 0 on success, a negative AVERROR on failure.
* example:
* avplayer* play = alloc_avplay_context();
* int ret;
* source_context sc = alloc_media_source(MEDIA_TYPE_FILE, "test.mp4", strlen("test.mp4") + 1, filesize("test.mp4"));
* ret = initialize(play, sc);
* if (ret != 0)
*    return ret; // ERROR!
*/
EXPORT_API int initialize(avplay *play, source_context *sc);

/*
* The Configure render or source to palyer.
* @param play pointer to the player.
* @param param video render or audio render or media_source.
* @param type Specifies render type, MEDIA_SOURCE	or AUDIO_RENDER VIDEO_RENDER.
* @This function does not return a value.
*/
EXPORT_API void configure(avplay *play, void *param, int type);

/*
* The start action player to play.
* @param play pointer to the player.
* @param fact at time, percent of duration.
* @param index Specifies the index of the file to play.
* @param Returns 0 if successful, or an error value otherwise.
*/
EXPORT_API int av_start(avplay *play, double fact, int index);

/*
* Wait for playback to complete.
* @param play pointer to the player.
* @This function does not return a value.
*/
EXPORT_API void wait_for_completion(avplay *play);

/*
* The Stop function stops playing the media.
* @param play pointer to the player.
* @This function does not return a value.
*/
EXPORT_API void av_stop(avplay *play);

/*
* The Pause method pauses the current player.
* @param play pointer to the player.
* @This function does not return a value.
*/
EXPORT_API void av_pause(avplay *play);

/*
* The Resume function starts the player from the current position, after a Pause function call.
* @param play pointer to the player.
* @This function does not return a value.
*/
EXPORT_API void av_resume(avplay *play);

/*
* Moves the current seek percent.
* @param play pointer to the player.
* @param fact at time, percent of duration.
* @This function does not return a value.
*/
EXPORT_API void av_seek(avplay *play, double fact);

/* Set audio volume.
* @param play pointer to the player.
* @param l is left channel.
* @param r is right channel.
* @param Returns 0 if successful, or an error value otherwise.
*/
EXPORT_API int av_volume(avplay *play, double l, double r);

/*
* Audio device is inited.
* @param play pointer to the player.
* @param Returns 0 if successful, or an error value otherwise.
*/
EXPORT_API int audio_is_inited(avplay *play);

/* Sets mute.
* @param play pointer to the player.
* @param vol is mute.
* @This function does not return a value.
*/
EXPORT_API void av_mute_set(avplay *play, int s);

/*
* The current playback time position
* @param play pointer to the player.
* @return current play time position, a negative on failure.
*/
EXPORT_API double av_curr_play_time(avplay *play);

/*
* The Duration function return the playing duration of the media, in second units.
* @param play pointer to the player.
* @return the playing duration of the media, in second units.
*/
EXPORT_API double av_duration(avplay *play);

EXPORT_API play_status av_status(avplay* play);

EXPORT_API int av_width(avplay* play);

EXPORT_API int av_height(avplay* play);

/*
* Destroys an player.
* @param play pointer to the player.
* @This function does not return a value.
*/
EXPORT_API void av_destory(avplay *play);

/*
* Allows the calculation of the real-time frame rate.
* @param play pointer to the player.
* @This function does not return a value.
*/
EXPORT_API void enable_calc_frame_rate(avplay *play);

/*
* Allows the calculation of the real-time bit rate.
* @param play pointer to the player.
* @This function does not return a value.
*/
EXPORT_API void enable_calc_bit_rate(avplay *play);

/*
* Get current real-time bit rate.
* @param play pointer to the player.
* @This function return bit rate(kpbs).
*/
EXPORT_API int current_bit_rate(avplay *play);

/*
* Get current real-time frame rate.
* @param play pointer to the player.
* @This function return frame rate(fps).
*/
EXPORT_API int current_frame_rate(avplay *play);

/*
* Get buffer progress.
* @param play pointer to the player.
* @This function return buffering(percent).
*/
EXPORT_API double buffering(avplay *play);

__EXTERN_C_END

#endif /* AVPLAY_H_ */
