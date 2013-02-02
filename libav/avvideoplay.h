/*
* avvideoplay.h
*/

#ifndef __AV_VIDEO_PLAY_H__
#define __AV_VIDEO_PLAY_H__

#include "globals.h"
#include "avqueue.h"
//#include "avplay.h"

class VMPlayer;

class VideoPlayer
{
public:
    VideoPlayer(VMPlayer* play, AVStream* stream);
    ~VideoPlayer();

    void set_render(vo_context* render);

    int start();

    int stop();

    void enable_calc_frame_rate();

    int get_frame_rate();

    /* 时钟函数. */
    double clock();

    void put_packet(AVPacket* packet);

    void put_flush_packet();

    bool is_rending();

protected:
    void update_pts(double pts, int64_t pos);

    void dec_thrd();

    void render_thrd();

    /* 视频帧复制. */
    void video_copy(AVFrame *dst, AVFrame *src);

    static void* dec_thrd_entry(void* param);
    static void* render_thrd_entry(void* param);

private:
    AVStream* video_stream_;
    AVCodecContext *m_video_ctx;

    vo_context *m_vo_ctx;

    /* 重采样音频指针.	*/
    struct SwsContext *m_swsctx;

    /* 视频队列.	*/
    av_queue m_video_q;
    av_queue m_video_dq;

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

    VMPlayer* m_play;

    /* 停止标志.	*/
    bool m_abort;

    bool m_rendering;
};

#endif /*__AV_VIDEO_PLAY_H__*/
