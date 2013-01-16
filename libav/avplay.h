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


struct AVFormatContext;
struct ReSampleContext;
struct AVCodecContext;
struct AVStream;


/* 播放器状态. */
typedef enum play_status
{
    inited, playing, paused, completed, stoped
} play_status;

enum sync_type
{
    AV_SYNC_AUDIO_MASTER, /* 默认选择. */
    AV_SYNC_VIDEO_MASTER, /* 同步到视频时间戳. */
    AV_SYNC_EXTERNAL_CLOCK, /* 同步到外部时钟. */
};

#define AVDECODE_BUFFER_SIZE	2

#define AV_SYNC_THRESHOLD		0.01f
#define AV_NOSYNC_THRESHOLD		10.0f
#define AUDIO_DIFF_AVG_NB		20

#define SEEKING_FLAG			-1
#define NOSEEKING_FLAG			0

/* 用于config_render参数表示所配置的render.  */
#define MEDIA_SOURCE			0
#define AUDIO_RENDER			1
#define VIDEO_RENDER			2

/* 用于标识渲染器类型. */
#define VIDEO_RENDER_D3D		0
#define VIDEO_RENDER_DDRAW		1
#define VIDEO_RENDER_OPENGL		2
#define VIDEO_RENDER_SOFT		3

typedef struct avplay
{
    /* 文件打开指针. */
    AVFormatContext *m_format_ctx;

    /* 音视频队列.	*/
    av_queue m_audio_q;
    av_queue m_video_q;
    av_queue m_audio_dq;
    av_queue m_video_dq;

    /* 各解码渲染线程.	*/
    pthread_t m_read_pkt_thrd;

    /* 音频和视频的AVStream、AVCodecContext指针和index.	*/
    AVStream *m_audio_st;
    AVStream *m_video_st;
    int m_audio_index;
    int m_video_index;

    /* 读取数据包占用缓冲大小.	*/
    long volatile m_pkt_buffer_size;
    pthread_mutex_t m_buf_size_mtx;

    /* 同步类型. */
    int m_av_sync_type;

    /* seek实现. */
    int m_read_pause_return;
    int m_seek_req;
    int m_seek_flags;
    int64_t m_seek_pos;
    int64_t m_seek_rel;
    int m_seek_by_bytes;
    int m_seeking;

    /* 最后一个解码帧的pts, 解码帧缓冲大小为2, 也就是当前播放帧的下一帧.	*/
    double m_external_clock;
    double m_external_clock_time;

    /* 当前数据源读取器. */
    source_context *m_source_ctx;
    AVIOContext *m_avio_ctx;
    unsigned char *m_io_buffer;

    /* 当前音频渲染器.	*/
    ao_context *m_ao_ctx;
    /* 当前视频渲染器. */
    vo_context *m_vo_ctx;
    /* 当前音频渲染器是否已经初始化完成, 为1表示完成初始化, 0表示未完成初始化. */
    int m_ao_inited;

    /* 播放状态. */
    play_status m_play_status;
    int m_rendering;

    /* 实时视频输入位率. */
    int m_enable_calc_video_bite;
    int m_real_bit_rate;
    int m_read_bytes[MAX_CALC_SEC]; /* 记录5秒内的字节数. */
    int m_last_vb_time;
    int m_vb_index;

    /* 正在播放的索引, 只用于BT文件播放. */
    int m_current_play_index;
    double m_start_time;
    double m_buffering;

    /* 停止标志.	*/
    int m_abort;

    avaudioplay* m_audioplay;
    avvideoplay* m_videoplay;

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

/*
* Blurring algorithm to the input video.
* @param frame pointer to the frame.
* @param fw is the width of the video.
* @param fh is the height of the video.
* @param dx is the x start coordinates of the target location.
* @param dy is the y start coordinates of the target location.
* @param dcx is width of the target range.
* @param dcx is height of the target range.
*/
EXPORT_API void blurring(AVFrame *frame,
                         int fw, int fh, int dx, int dy, int dcx, int dcy);

/*
* Alpha blend image mixing.
* @param frame pointer to the frame.
* @param rgba pointer to the RGBA image.
* @param fw is the width of the video.
* @param fh is the height of the video.
* @param rgba_w is the width of the image.
* @param rgba_h is the height of the image.
* @param x is the x start coordinates of the target location.
* @param y is the y start coordinates of the target location.
*/
EXPORT_API void alpha_blend(AVFrame *frame, uint8_t *rgba,
                            int fw, int fh, int rgba_w, int rgba_h, int x, int y);
__EXTERN_C_END

/********************************************************************************
for avvideoplay
********************************************************************************/
double master_clock(avplay *play);
int open_decoder(AVCodecContext *ctx);

#endif /* AVPLAY_H_ */
