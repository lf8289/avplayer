#include "avaudioplay.h"
#include "mediaplayer.h"

AudioPlayer::AudioPlayer(VMPlayer* play, AVStream* stream)
: m_audio_ctx(NULL)
, m_swr_ctx(NULL)
, m_ao_ctx(NULL)
, m_ao_inited(false)
, m_resample_ctx(NULL)
, m_audio_clock(0.0)
, m_audio_buf_size(0)
, m_audio_buf_index(0)
, m_audio_write_buf_size(0)
, m_audio_current_pts_drift(0)
, m_audio_current_pts_last(0)
//, m_audio_render_thrd(-1)
//, m_audio_dec_thrd(-1)
, m_play(NULL)
, m_abort(false)
{
    m_play = play;
    audio_stream_ = stream;
    m_audio_ctx = stream->codec;
}

AudioPlayer::~AudioPlayer()
{
}

void AudioPlayer::set_render(ao_context* render)
{
    m_ao_ctx = render;
}

int AudioPlayer::start()
{
    pthread_attr_t attr;
    int ret = 0;

    queue_set_type(&m_audio_q, QUEUE_PACKET);
    queue_init(&m_audio_q);
    queue_set_type(&m_audio_dq, QUEUE_AVFRAME);
    queue_init(&m_audio_dq);

    ret = m_play->open_decoder(m_audio_ctx);
    if (ret != 0) {
        return ret;
    }

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    ret = pthread_create(&m_audio_dec_thrd, &attr, dec_thrd_entry,(void*)this);
    if (ret)
    {
        printf("ERROR; return code from pthread_create() is %d\n", ret);
        return ret;
    }

    ret = pthread_create(&m_audio_render_thrd, &attr, render_thrd_entry, (void*)this);
    if (ret)
    {
        printf("ERROR; return code from pthread_create() is %d\n", ret);
        return ret;
    }

    return ret;
}

int AudioPlayer::stop()
{
    void *status = NULL;

    m_abort = true;

    queue_stop(&m_audio_q);
    queue_stop(&m_audio_dq);

    pthread_join(m_audio_dec_thrd, &status);
    pthread_join(m_audio_render_thrd, &status);

    queue_end(&m_audio_q);
    queue_end(&m_audio_dq);

    if (m_swr_ctx) {
        swr_free(&m_swr_ctx);
        m_swr_ctx = NULL;
    }
    if (m_resample_ctx) {
        audio_resample_close(m_resample_ctx);
        m_resample_ctx = NULL;
    }
    if (m_audio_ctx) {
        avcodec_close(m_audio_ctx);
        m_audio_ctx = NULL;
    }

    return 0;
}

int AudioPlayer::set_volume(double l, double r)
{
    if (m_ao_inited)
    {
        m_ao_ctx->audio_control(m_ao_ctx, l, r);
        return 0;
    }

    return -1;
}

void AudioPlayer::set_mute(int s)
{
    m_ao_ctx->mute_set(m_ao_ctx, s);
}

/* 时钟函数. */
double AudioPlayer::clock()
{
    double pts;
    int hw_buf_size, bytes_per_sec;
    pts = m_audio_clock;
    hw_buf_size = m_audio_buf_size - m_audio_buf_index;
    bytes_per_sec = 0;
    if (audio_stream_) {
        bytes_per_sec = audio_stream_->codec->sample_rate * 2
            * FFMIN(audio_stream_->codec->channels, 2); /* 固定为2通道.	*/
    }

    if (fabs(m_audio_current_pts_drift) <= 1.0e-6)
    {
#if 0
        if (fabs(m_play->m_start_time) > 1.0e-6)
            m_audio_current_pts_drift = pts - m_audio_current_pts_last;
        else
#endif
            m_audio_current_pts_drift = pts;
    }

    if (bytes_per_sec)
        pts -= (double) hw_buf_size / bytes_per_sec;
    return pts - m_audio_current_pts_drift;
}

void AudioPlayer::put_packet(AVPacket* packet)
{
    put_queue(&m_audio_q, packet);
}

void AudioPlayer::put_flush_packet()
{
    queue_flush(&m_audio_q);
    put_queue(&m_audio_q, &flush_pkt);
}

void AudioPlayer::dec_thrd()
{
    AVPacket pkt, pkt2;
    int ret, n;
    AVFrame avframe = { 0 }, avcopy;
    int64_t v_start_time = 0;
    int64_t a_start_time = 0;
    AVStream* video_st = m_play->get_video_stream();

    if (video_st && audio_stream_)
    {
        v_start_time = video_st->start_time;
        a_start_time = audio_stream_->start_time;
    }

    for (; !m_abort;)
    {
        av_init_packet(&pkt);
        while (m_play->get_status() == paused && !m_abort)
            Sleep(10);
        ret = get_queue(&m_audio_q, &pkt);
        if (ret != -1)
        {
            if (pkt.data == flush_pkt.data)
            {
                AVFrameList* lst = NULL;
                avcodec_flush_buffers(m_audio_ctx);
                while (m_audio_dq.m_size && !m_abort)
                    Sleep(1);
                pthread_mutex_lock(&m_audio_dq.m_mutex);
                lst = (AVFrameList*)m_audio_dq.m_first_pkt;
                for (; lst != NULL; lst = lst->next)
                    lst->pkt.type = 1;	/*type为1表示skip.*/
                pthread_mutex_unlock(&m_audio_dq.m_mutex);
                continue;
            }

            /* 使用pts更新音频时钟. */
            if (pkt.pts != AV_NOPTS_VALUE)
                m_audio_clock = av_q2d(audio_stream_->time_base) * (pkt.pts - v_start_time);

            if (fabs(m_audio_current_pts_last) < 1.0e-6)
                m_audio_current_pts_last = m_audio_clock;

            /* 计算pkt缓冲数据大小. */
            m_play->buffer_size_add(-pkt.size);

            /* 解码音频. */
            pkt2 = pkt;
            avcodec_get_frame_defaults(&avframe);

            while (!m_abort)
            {
                int got_frame = 0;
                ret = avcodec_decode_audio4(m_audio_ctx, &avframe, &got_frame, &pkt2);
                if (ret < 0)
                {
                    printf("Audio error while decoding one frame!!!\n");
                    break;
                }
                pkt2.size -= ret;
                pkt2.data += ret;

                /* 不足一个帧, 并且packet中还有数据, 继续解码当前音频packet. */
                if (!got_frame && pkt2.size > 0)
                    continue;

                /* packet中已经没有数据了, 并且不足一个帧, 丢弃这个音频packet. */
                if (pkt2.size == 0 && !got_frame)
                    break;

                if (avframe.linesize[0] != 0)
                {
                    /* copy并转换音频格式. */
                    audio_copy(&avcopy, &avframe);

                    /* 将计算的pts复制到avcopy.pts.  */
                    memcpy(&avcopy.pts, &m_audio_clock, sizeof(double));

                    /* 计算下一个audio的pts值.  */
                    n = 2 * FFMIN(m_audio_ctx->channels, 2);

                    m_audio_clock += ((double) avcopy.linesize[0] / (double) (n * m_audio_ctx->sample_rate));

                    /* 如果不是以音频同步为主, 则需要计算是否移除一些采样以同步到其它方式.	*/
                    if (m_play->get_sync_type() == AV_SYNC_EXTERNAL_CLOCK ||
                        m_play->get_sync_type() == AV_SYNC_VIDEO_MASTER && video_st)
                    {
                        /* 暂无实现.	*/
                    }

                    /* 防止内存过大.	*/
                    chk_queue(&m_audio_dq, AVDECODE_BUFFER_SIZE);

                    /* 丢到播放队列中.	*/
                    put_queue(&m_audio_dq, &avcopy);

                    /* packet中数据已经没有数据了, 解码下一个音频packet. */
                    if (pkt2.size <= 0)
                        break;
                }
            }
            av_free_packet(&pkt);
        }
    }
}

void AudioPlayer::render_thrd()
{
    AVFrame audio_frame;
    int audio_size = 0;
    int ret, temp, inited = 0;
    int bytes_per_sec;

    while (!m_abort)
    {
        ret = get_queue(&m_audio_dq, &audio_frame);
        if (audio_frame.data[0] == flush_frm.data[0])
            continue;
        if (ret != -1)
        {
            if (!m_ao_inited && m_ao_ctx)
            {
                inited = 1;
                /* 配置渲染器. */
                ret = m_ao_ctx->init_audio(m_ao_ctx,
                    FFMIN(m_audio_ctx->channels, 2), 16, m_audio_ctx->sample_rate, 0);
                if (ret != 0)
                    inited = -1;
                else
                {
                    /* 更改播放状态. */
                    m_play->set_status(playing);
                }
                bytes_per_sec = m_audio_ctx->sample_rate *
                    FFMIN(m_audio_ctx->channels, 2) * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                /* 修改音频设备初始化状态, 置为1. */
                if (inited != -1)
                    m_ao_inited = true;
            }
            else if (!m_ao_ctx)
            {
                av_free(audio_frame.data[0]);
                break;
            }

            if (audio_frame.type == 1)
            {
                av_free(audio_frame.data[0]);
                continue;
            }

            audio_size = audio_frame.linesize[0];
            /* 清空. */
            m_audio_buf_size = audio_size;
            m_audio_buf_index = 0;

            /* 已经开始播放, 清空seeking的状态. */
            m_play->set_seek_status(NOSEEKING_FLAG);

            while (audio_size > 0)
            {
                if (inited == 1 && m_ao_ctx)
                {
                    temp = m_ao_ctx->play_audio(m_ao_ctx,
                        audio_frame.data[0] + m_audio_buf_index, m_audio_buf_size - m_audio_buf_index);
                    m_audio_buf_index += temp;
                    /* 如果缓冲已满, 则休眠一小会. */
                    if (temp == 0)
                    {
                        if (m_audio_dq.m_size > 0)
                        {
                            if (((AVFrameList*) m_audio_dq.m_last_pkt)->pkt.type == 1)
                                break;
                        }
                        Sleep(10);
                    }
                }
                else
                {
                    assert(0);
                }
                audio_size = m_audio_buf_size - m_audio_buf_index;
            }

            av_free(audio_frame.data[0]);
        }
    }
}

void AudioPlayer::audio_copy(AVFrame *dst, AVFrame *src)
{
    int nb_sample;
    int dst_buf_size;
    int out_channels;

    dst->linesize[0] = src->linesize[0];
    *dst = *src;
    dst->data[0] = NULL;
    dst->type = 0;
    out_channels = m_audio_ctx->channels;/* (FFMIN(m_audio_ctx->channels, 2)); */
    nb_sample = src->linesize[0] / m_audio_ctx->channels / av_get_bytes_per_sample(m_audio_ctx->sample_fmt);
    dst_buf_size = nb_sample * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * out_channels;
    dst->data[0] = (uint8_t*) av_malloc(dst_buf_size);
    assert(dst->data[0]);
    avcodec_fill_audio_frame(dst, out_channels, AV_SAMPLE_FMT_S16, dst->data[0], dst_buf_size, 0);

    /* 重采样到AV_SAMPLE_FMT_S16格式. */
    if (m_audio_ctx->sample_fmt != AV_SAMPLE_FMT_S16)
    {
        if (!m_swr_ctx)
        {
            uint64_t in_channel_layout = av_get_default_channel_layout(m_audio_ctx->channels);
            uint64_t out_channel_layout = av_get_default_channel_layout(out_channels);
            m_swr_ctx = swr_alloc_set_opts(NULL, in_channel_layout, AV_SAMPLE_FMT_S16,
                m_audio_ctx->sample_rate, in_channel_layout,
                m_audio_ctx->sample_fmt, m_audio_ctx->sample_rate, 0, NULL);
            swr_init(m_swr_ctx);
        }

        if (m_swr_ctx)
        {
            int ret, out_count;
            out_count = dst_buf_size / out_channels / av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            ret = swr_convert(m_swr_ctx, dst->data, out_count, (const uint8_t **)src->data, nb_sample);
            if (ret < 0)
                assert(0);
            src->linesize[0] = dst->linesize[0] = ret * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * out_channels;
            memcpy(src->data[0], dst->data[0], src->linesize[0]);
        }
    }

    /* 重采样到双声道. */
    if (m_audio_ctx->channels > 2)
    {
        if (!m_resample_ctx)
        {
            m_resample_ctx = av_audio_resample_init(
                FFMIN(2, m_audio_ctx->channels),
                m_audio_ctx->channels, m_audio_ctx->sample_rate,
                m_audio_ctx->sample_rate, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16,
                16, 10, 0, 0.f);
        }

        if (m_resample_ctx)
        {
            int samples = src->linesize[0] / (av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * m_audio_ctx->channels);
            dst->linesize[0] = audio_resample(m_resample_ctx,
                (short *) dst->data[0], (short *) src->data[0], samples) * 4;
        }
    }
    else
    {
        dst->linesize[0] = dst->linesize[0];
        memcpy(dst->data[0], src->data[0], dst->linesize[0]);
    }
}

void* AudioPlayer::dec_thrd_entry(void* param)
{
    AudioPlayer* player = (AudioPlayer*)param;
    player->dec_thrd();

    return NULL;
}

void* AudioPlayer::render_thrd_entry(void* param)
{
    AudioPlayer* player = (AudioPlayer*)param;
    player->render_thrd();

    return NULL;
}