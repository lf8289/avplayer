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
    /* �ļ���ָ��. */
    AVFormatContext *m_format_ctx;

    /* demux�߳�.	*/
    pthread_t m_read_pkt_thrd;

    /* ��Ƶ����Ƶ��AVStream��AVCodecContextָ���index.	*/
    AVStream *m_audio_st;
    AVStream *m_video_st;
    int m_audio_index;
    int m_video_index;

    /* ��ȡ���ݰ�ռ�û����С.	*/
    long volatile m_pkt_buffer_size;
    pthread_mutex_t m_buf_size_mtx;

    /* ͬ������. */
    int m_av_sync_type;

    /* seekʵ��. */
    int m_read_pause_return;
    int m_seek_req;
    int m_seek_flags;
    int64_t m_seek_pos;
    int64_t m_seek_rel;
    int m_seek_by_bytes;
    int m_seeking;

    /* ���һ������֡��pts, ����֡�����СΪ2, Ҳ���ǵ�ǰ����֡����һ֡.	*/
    double m_external_clock;
    double m_external_clock_time;

    /* ��ǰ����Դ��ȡ��. */
    source_context *m_source_ctx;
    AVIOContext *m_avio_ctx;
    unsigned char *m_io_buffer;

    /* ��ǰ��Ƶ��Ⱦ��.	*/
//    ao_context *m_ao_ctx;
    /* ��ǰ��Ƶ��Ⱦ��. */
    //vo_context *m_vo_ctx;
    /* ��ǰ��Ƶ��Ⱦ���Ƿ��Ѿ���ʼ�����, Ϊ1��ʾ��ɳ�ʼ��, 0��ʾδ��ɳ�ʼ��. */
    int m_ao_inited;

    /* ����״̬. */
    play_status m_play_status;

    /* ʵʱ��Ƶ����λ��. */
    int m_enable_calc_video_bite;
    int m_real_bit_rate;
    int m_read_bytes[MAX_CALC_SEC]; /* ��¼5���ڵ��ֽ���. */
    int m_last_vb_time;
    int m_vb_index;

    /* ���ڲ��ŵ�����, ֻ����BT�ļ�����. */
    int m_current_play_index;

    double m_start_time;
    double m_buffering;

    /* ֹͣ��־.	*/
    int m_abort;

    AudioPlayer* audio_player_;
    VideoPlayer* video_player_;
};

#endif // FFMPEG_MEDIAPLAYER_H
