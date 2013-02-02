/*
* avaudio.h
*/

#ifndef __AV_AUDIO_H__
#define __AV_AUDIO_H__

#include "globals.h"
#include "avqueue.h"
//#include "avplay.h"

class VMPlayer;

class AudioPlayer
{
public:
    AudioPlayer(VMPlayer* play, AVStream* stream);
    ~AudioPlayer();

    void set_render(ao_context* render);

    int start();

    int stop();

    int set_volume(double l, double r);

    void set_mute(int s);

    /* 时钟函数. */
    double clock();

    void put_packet(AVPacket* packet);

    void put_flush_packet();

protected:
    /* 解码线程*/
    void dec_thrd();

    /* 渲染线程. */
    void render_thrd();

    /* 视频帧复制. */
    void audio_copy(AVFrame *dst, AVFrame *src);

    static void* dec_thrd_entry(void* param);
    static void* render_thrd_entry(void* param);

private:
    AVStream* audio_stream_;
    AVCodecContext *m_audio_ctx;

    /* 当前音频渲染器.	*/
    ao_context *m_ao_ctx;
    bool m_ao_inited;

    /* 重采样音频指针.	*/
    struct SwrContext *m_swr_ctx;
    ReSampleContext *m_resample_ctx;

    /* 音频队列.	*/
    av_queue m_audio_q;
    av_queue m_audio_dq;

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

    VMPlayer* m_play;

    /* 停止标志.	*/
    bool m_abort;
};

#endif /* AVPLAY_H_ */
