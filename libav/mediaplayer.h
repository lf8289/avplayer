#ifndef FFMPEG_MEDIAPLAYER_H
#define FFMPEG_MEDIAPLAYER_H

#include "globals.h"
#include "avvideoplay.h"
#include "avaudioplay.h"

class VMPlayer {
public:
    VMPlayer();
    ~VMPlayer();

    int initialize(source_context *sc);

    void configure(void *param, int type);

    int av_start(double fact, int index);

    void wait_for_completion();

    void av_stop();

    void av_pause();

    void av_resume();

    void av_seek(double fact);

    int av_volume(double l, double r);

    int audio_is_inited();

    void av_mute_set(int s);

    double av_curr_play_time();

    double av_duration();

    void enable_calc_frame_rate();

    void enable_calc_bit_rate();

    int current_bit_rate();

    int current_frame_rate();

    int width();

    int height();

    double buffering();

public:
    double master_clock();
    int open_decoder(AVCodecContext *ctx);
    AVStream* get_video_stream();
    AVStream* get_audio_stream();
    play_status get_status();
    void set_status(play_status status);
    void set_seek_status(int status);
    int get_sync_type();
    void buffer_size_add(int diff);

protected:
    void wait_for_threads();
    media_info* find_media_info(source_context *sc, int index);
    int stream_index(enum AVMediaType type, AVFormatContext *ctx);
    double external_clock();
    void read_pkt_thrd();

    static int decode_interrupt_cb(void *ctx);
    static int64_t seek_packet(void *opaque, int64_t offset, int whence);
    static int read_packet(void *opaque, uint8_t *buf, int buf_size);

    static void* read_pkt_thrd_entry(void* param);

private:
    /* 文件打开指针. */
    AVFormatContext *m_format_ctx;

    /* demux线程.	*/
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
//    ao_context *m_ao_ctx;
    /* 当前视频渲染器. */
    //vo_context *m_vo_ctx;
    /* 当前音频渲染器是否已经初始化完成, 为1表示完成初始化, 0表示未完成初始化. */
    int m_ao_inited;

    /* 播放状态. */
    play_status m_play_status;

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

    AudioPlayer* audio_player_;
    VideoPlayer* video_player_;
};

#endif // FFMPEG_MEDIAPLAYER_H
