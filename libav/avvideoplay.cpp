#include "avvideoplay.h"
#include "mediaplayer.h"

extern AVPacket flush_pkt;
extern AVFrame flush_frm;

VideoPlayer::VideoPlayer(VMPlayer* play, AVStream* stream)
: video_stream_(NULL)
, m_video_ctx(NULL)
, m_vo_ctx(NULL)
, m_swsctx(NULL)
, m_video_clock(0)
, m_video_current_pts_drift(0)
, m_video_current_pts(0)
, m_frame_timer(0)
, m_frame_last_pts(0)
, m_frame_last_duration(0)
, m_frame_last_delay(0)
, m_frame_last_filter_delay(0)
, m_frame_last_dropped_pts(0)
, m_frame_last_returned_time(0)
, m_frame_last_dropped_pos(0)
, m_video_current_pos(0)
, m_drop_frame_num(0)
, m_enable_calc_frame_rate(0)
, m_real_frame_rate(0)
, m_last_fr_time(0)
, m_fr_index(0)
, m_play(NULL)
, m_abort(false)
, m_rendering(false)
{
    m_play = play;
    video_stream_ = stream;
    m_video_ctx = stream->codec;
}

VideoPlayer::~VideoPlayer()
{
}

int VideoPlayer::start()
{
    pthread_attr_t attr;
    int ret = 0;

    queue_set_type(&m_video_q, QUEUE_PACKET);
    queue_init(&m_video_q);
    queue_set_type(&m_video_dq, QUEUE_AVFRAME);
    queue_init(&m_video_dq);

    ret = m_play->open_decoder(m_video_ctx);
    if (ret != 0) {
        return ret;
    }

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    ret = pthread_create(&m_video_dec_thrd, &attr, dec_thrd_entry,(void*)this);
    if (ret)
    {
        printf("ERROR; return code from pthread_create() is %d\n", ret);
        return ret;
    }

    ret = pthread_create(&m_video_render_thrd, &attr, render_thrd_entry, (void*)this);
    if (ret)
    {
        printf("ERROR; return code from pthread_create() is %d\n", ret);
        return ret;
    }

    return ret;
}

void VideoPlayer::set_render(vo_context* render)
{
    m_vo_ctx = render;
}

int VideoPlayer::stop()
{
    void *status = NULL;

    m_abort = true;

    queue_stop(&m_video_q);
    queue_stop(&m_video_dq);
    pthread_join(m_video_dec_thrd, &status);
    pthread_join(m_video_render_thrd, &status);
    queue_end(&m_video_q);
    queue_end(&m_video_dq);

    if (m_swsctx != NULL) {
        sws_freeContext(m_swsctx);
        m_swsctx = NULL;
    }

    if (m_video_ctx) {
        avcodec_close(m_video_ctx);
        m_video_ctx = NULL;
    }

    return 0;
}


void  VideoPlayer::enable_calc_frame_rate()
{
    m_enable_calc_frame_rate = 1;
}

int VideoPlayer::get_frame_rate()
{
    return m_real_frame_rate;
}

/* 时钟函数. */
double VideoPlayer::clock()
{
    if (m_play->get_status() == paused)
        return m_video_current_pts;
    return m_video_current_pts_drift + av_gettime() / 1000000.0f;
}

void VideoPlayer::put_packet(AVPacket* packet)
{
    put_queue(&m_video_q, packet);
}

void VideoPlayer::put_flush_packet()
{
    queue_flush(&m_video_q);
    put_queue(&m_video_q, &flush_pkt);
}

bool VideoPlayer::is_rending()
{
    return m_rendering;
}

void VideoPlayer::update_pts(double pts, int64_t pos)
{
    double time = av_gettime() / 1000000.0;
    /* update current video pts */
    m_video_current_pts = pts;
    m_video_current_pts_drift = m_video_current_pts - time;
    m_video_current_pos = pos;
    m_frame_last_pts = pts;
}

void VideoPlayer::dec_thrd()
{
    AVPacket pkt, pkt2;
    AVFrame *avframe, avcopy;
    int got_picture = 0;
    int ret = 0;
    int64_t v_start_time = 0;
    int64_t a_start_time = 0;
    AVStream* audio_st = m_play->get_audio_stream();

    avframe = avcodec_alloc_frame();

    if (video_stream_ && audio_st)
    {
        v_start_time = video_stream_->start_time;
        a_start_time = audio_st->start_time;
    }

    for (; !m_abort;)
    {
        av_init_packet(&pkt);
        while (m_play->get_status() == paused && !m_abort)
            Sleep(10);
        ret = get_queue(&m_video_q, (AVPacket*) &pkt);
        if (ret != -1)
        {
            if (pkt.data == flush_pkt.data)
            {
                AVFrameList* lst = NULL;

                avcodec_flush_buffers(m_video_ctx);

                while (m_video_dq.m_size && !m_abort)
                    Sleep(1);

                pthread_mutex_lock(&m_video_dq.m_mutex);
                lst = (AVFrameList*)m_video_dq.m_first_pkt;
                for (; lst != NULL; lst = lst->next)
                    lst->pkt.type = 1; /* type为1表示skip. */
                m_video_current_pos = -1;
                m_frame_last_dropped_pts = AV_NOPTS_VALUE;
                m_frame_last_duration = 0;
                m_frame_timer = (double) av_gettime() / 1000000.0f;
                m_video_current_pts_drift = -m_frame_timer;
                m_frame_last_pts = AV_NOPTS_VALUE;
                pthread_mutex_unlock(&m_video_dq.m_mutex);

                continue;
            }

            m_play->buffer_size_add(-pkt.size);
            pkt2 = pkt;

            while (pkt2.size > 0 && !m_abort)
            {
                ret = avcodec_decode_video2(m_video_ctx, avframe, &got_picture, &pkt2);
                if (ret < 0)
                {
                    printf("Video error while decoding one frame!!!\n");
                    break;
                }
                if (got_picture)
                    break;
                pkt2.size -= ret;
                pkt2.data += ret;
            }

            if (got_picture)
            {
                double pts1 = 0.0f;
                double frame_delay, pts;

                /*
                * 复制帧, 并输出为PIX_FMT_YUV420P.
                */

                video_copy( &avcopy, avframe);

                /*
                * 初始化m_frame_timer时间, 使用系统时间.
                */
                if (m_frame_timer == 0.0f)
                    m_frame_timer = (double) av_gettime() / 1000000.0f;

                /*
                * 计算pts值.
                */
                pts1 = (avcopy.best_effort_timestamp - a_start_time) * av_q2d(video_stream_->time_base);
                if (pts1 == AV_NOPTS_VALUE)
                    pts1 = 0;
                pts = pts1;

                /* 如果以音频同步为主, 则在此判断是否进行丢包. */
                if ((audio_st) &&
                    ((m_play->get_sync_type() == AV_SYNC_AUDIO_MASTER && audio_st)
                    || m_play->get_sync_type() == AV_SYNC_EXTERNAL_CLOCK))
                {
                    pthread_mutex_lock(&m_video_dq.m_mutex);
                    /*
                    * 最后帧的pts是否为AV_NOPTS_VALUE 且 pts不等于0
                    * 计算视频时钟和主时钟源的时间差.
                    * 计算pts时间差, 当前pts和上一帧的pts差值.
                    */
                    ret = 1;
                    if (m_frame_last_pts != AV_NOPTS_VALUE && pts)
                    {
                        double clockdiff = clock() - m_play->master_clock();
                        double ptsdiff = pts - m_frame_last_pts;

                        /*
                        * 如果clockdiff和ptsdiff同时都在同步阀值范围内
                        * 并且clockdiff与ptsdiff之和与m_frame_last_filter_delay的差
                        * 如果小于0, 则丢弃这个视频帧.
                        */
                        if (fabs(clockdiff) < AV_NOSYNC_THRESHOLD && ptsdiff > 0
                            && ptsdiff < AV_NOSYNC_THRESHOLD
                            && clockdiff + ptsdiff - m_frame_last_filter_delay < 0)
                        {
                            m_frame_last_dropped_pos = pkt.pos;
                            m_frame_last_dropped_pts = pts;
                            m_drop_frame_num++;
                            printf("\nDROP: %3d drop a frame of pts is: %.3f\n", m_drop_frame_num, pts);
                            ret = 0;
                        }
                    }
                    pthread_mutex_unlock(&m_video_dq.m_mutex);
                    if (ret == 0)
                    {
                        /* 丢掉该帧. */
                        av_free(avcopy.data[0]);
                        continue;
                    }
                }

                /* 计录最后有效帧时间. */
                m_frame_last_returned_time = av_gettime() / 1000000.0f;
                /* m_frame_last_filter_delay基本都是0吧. */
                m_frame_last_filter_delay = av_gettime() / 1000000.0f - m_frame_last_returned_time;
                /* 如果m_frame_last_filter_delay还可能大于1, 那么m_frame_last_filter_delay置0. */
                if (fabs(m_frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0f)
                    m_frame_last_filter_delay = 0.0f;

                /*
                *	更新当前m_video_clock为当前解码pts.
                */
                if (pts != 0)
                    m_video_clock = pts;
                else
                    pts = m_video_clock;

                /*
                *	计算当前帧的延迟时长.
                */
                frame_delay = av_q2d(m_video_ctx->time_base);
                frame_delay += avcopy.repeat_pict * (frame_delay * 0.5);

                /*
                * m_video_clock加上该帧延迟时长,
                * m_video_clock是估算出来的下一帧的pts.
                */
                m_video_clock += frame_delay;

                /*
                * 防止内存过大.
                */
                chk_queue(&m_video_dq, AVDECODE_BUFFER_SIZE);

                /* 保存frame_delay为该帧的duration, 保存到.pts字段中. */
                memcpy(&avcopy.pkt_dts, &frame_delay, sizeof(double));
                /* 保存pts. */
                memcpy(&avcopy.pts, &pts, sizeof(double));
                /* 保存pos, pos即是文件位置. */
                avcopy.pkt_pos = pkt.pos;
                /* type为0表示no skip. */
                avcopy.type = 0;

                /* 丢进播放队列.	*/
                put_queue(&m_video_dq, &avcopy);
            }
            av_free_packet(&pkt);
        }
    }
    av_free(avframe);
}

void VideoPlayer::render_thrd()
{
    AVFrame video_frame;
    int ret = 0;
    int inited = 0;
    double sync_threshold;
    double current_pts;
    double last_duration;
    double duration;
    double delay = 0.0f;
    double time;
    double next_pts;
    double diff = 0.0f;
    int64_t frame_num = 0;
    double diff_sum = 0;
    double avg_diff = 0.0f;
    AVStream* audio_st = m_play->get_audio_stream();

    while (!m_abort)
    {
        /* 如果视频队列为空 */
        if (m_video_dq.m_size == 0)
        {
            pthread_mutex_lock(&m_video_dq.m_mutex);
            /*
            * 如果最后丢弃帧的pts不为空, 且大于最后pts则
            * 使用最后丢弃帧的pts值更新其它相关的pts值.
            */
            if (m_frame_last_dropped_pts != AV_NOPTS_VALUE && m_frame_last_dropped_pts > m_frame_last_pts)
            {
                update_pts(m_frame_last_dropped_pts, m_frame_last_dropped_pos);
                m_frame_last_dropped_pts = AV_NOPTS_VALUE;
            }
            pthread_mutex_unlock(&m_video_dq.m_mutex);
        }
        /* 获得下一帧视频. */
        ret = get_queue(&m_video_dq, &video_frame);
        if (ret != -1)
        {
            // 状态为正在渲染.
            m_rendering = 1;
            // 如果没有初始化渲染器, 则初始化渲染器.
            if (!inited && m_vo_ctx)
            {
                inited = 1;
                m_vo_ctx->fps = (float)video_stream_->r_frame_rate.num / (float)video_stream_->r_frame_rate.den;
                ret = m_vo_ctx->init_video(m_vo_ctx,
                    m_video_ctx->width, m_video_ctx->height, m_video_ctx->pix_fmt);
                if (ret != 0)
                    inited = -1;
                else
                    m_play->set_status(playing);
            }

            if (video_frame.data[0] == flush_frm.data[0])
                continue;

            do {
                /* 判断是否skip. */
                if (video_frame.type == 1)
                {
                    /* 跳过该帧. */
                    break;
                }

                /* 计算last_duration. */
                memcpy(&current_pts, &video_frame.pts, sizeof(double));
                last_duration = current_pts - m_frame_last_pts;
                if (last_duration > 0 && last_duration < 10.0)
                {
                    /* 更新m_frame_last_duration. */
                    m_frame_last_duration = last_duration;
                }

                /* 更新延迟同步到主时钟源. */
                delay = m_frame_last_duration;
                if ((m_play->get_sync_type() == AV_SYNC_EXTERNAL_CLOCK) ||
                    (m_play->get_sync_type() == AV_SYNC_AUDIO_MASTER && audio_st))
                {
                    diff = clock() - m_play->master_clock();
                    sync_threshold = FFMAX(AV_SYNC_THRESHOLD, delay) * 0.75;
                    if (fabs(diff) < AV_NOSYNC_THRESHOLD)
                    {
                        if (diff <= -sync_threshold)
                            delay = 0.0f;
                        else if (diff >= sync_threshold)
                            delay = 2.0f * delay;
                    }
                    else
                    {
                        if (diff < 0.0f)
                            delay = 0.0f;
                        else
                            Sleep(0);
                    }
                }

                /* 得到当前系统时间. */
                time = av_gettime() / 1000000.0f;

                /* 如果当前系统时间小于播放时间加延迟时间, 则过一会重试. */
                if (time < m_frame_timer + delay)
                {
                    Sleep(1);
                    continue;
                }

                /* 更新m_frame_timer. */
                if (delay > 0.0f)
                    m_frame_timer += delay * FFMAX(1, floor((time - m_frame_timer) / delay));

                pthread_mutex_lock(&m_video_dq.m_mutex);
                update_pts(current_pts, video_frame.pkt_pos);
                pthread_mutex_unlock(&m_video_dq.m_mutex);

                /* 计算下一帧的时间.  */
                if (m_video_dq.m_size > 0)
                {
                    memcpy(&next_pts, &(((AVFrameList*) m_video_dq.m_last_pkt)->pkt.pts), sizeof(double));
                    duration = next_pts - current_pts;
                }
                else
                {
                    memcpy(&duration, &video_frame.pkt_dts, sizeof(double));
                }

                if (audio_st && time > m_frame_timer + duration)
                {
                    if (m_video_dq.m_size > 1)
                    {
                        pthread_mutex_lock(&m_video_dq.m_mutex);
                        m_drop_frame_num++;
                        pthread_mutex_unlock(&m_video_dq.m_mutex);
                        printf("\nDROP: %3d drop a frame of pts is: %.3f\n", m_drop_frame_num, current_pts);
                        break;
                    }
                }

                if (diff < 1000)
                {
                    frame_num++;
                    diff_sum += fabs(diff);
                    avg_diff = (double)diff_sum / frame_num;
                }
                //printf("%7.3f A-V: %7.3f A: %7.3f V: %7.3f FR: %d/fps, VB: %d/kbps\r",
                //master_clock(play), diff, audio_clock(play), video_clock(play), play->m_real_frame_rate, play->m_real_bit_rate);

                /*	在这里计算帧率.	*/
                if (m_enable_calc_frame_rate)
                {
                    int current_time = 0;
                    /* 计算时间是否足够一秒钟. */
                    if (m_last_fr_time == 0)
                        m_last_fr_time = av_gettime() / 1000000.0f;
                    current_time = av_gettime() / 1000000.0f;
                    if (current_time - m_last_fr_time >= 1)
                    {
                        m_last_fr_time = current_time;
                        if (++m_fr_index == MAX_CALC_SEC)
                            m_fr_index = 0;

                        /* 计算frame_rate. */
                        do
                        {
                            int sum = 0;
                            int i = 0;
                            for (; i < MAX_CALC_SEC; i++)
                                sum += m_frame_num[i];
                            m_real_frame_rate = (double)sum / (double)MAX_CALC_SEC;
                        } while (0);
                        /* 清空. */
                        m_frame_num[m_fr_index] = 0;
                    }

                    /* 更新读取字节数. */
                    m_frame_num[m_fr_index]++;
                }

                /* 已经开始播放, 清空seeking的状态. */
                //if (m_play->m_seeking == SEEKING_FLAG)
                //    m_play->m_seeking = NOSEEKING_FLAG;
                m_play->set_seek_status(NOSEEKING_FLAG);

                if (inited == 1 && m_vo_ctx)
                {
                    m_vo_ctx->render_one_frame(m_vo_ctx, &video_frame, m_video_ctx->pix_fmt, m_play->av_curr_play_time());
                    if (delay != 0)
                        Sleep(4);
                }
                break;
            } while (TRUE);

            /* 渲染完成. */
            m_rendering = 0;

            /* 如果处于暂停状态, 则直接渲染窗口, 以免黑屏. */
            while (m_play->get_status() == paused && inited == 1 && m_vo_ctx && !m_abort)
            {
                m_vo_ctx->render_one_frame(m_vo_ctx, &video_frame, m_video_ctx->pix_fmt, m_play->av_curr_play_time());
                Sleep(16);
            }

            /* 释放视频帧缓冲. */
            av_free(video_frame.data[0]);
        }
    }
}

/* 视频帧复制. */
void VideoPlayer::video_copy(AVFrame *dst, AVFrame *src)
{
    uint8_t *buffer;
    int len = avpicture_get_size(PIX_FMT_YUV420P, m_video_ctx->width, m_video_ctx->height);
    *dst = *src;
    buffer = (uint8_t*) av_malloc(len);
    assert(buffer);

    avpicture_fill((AVPicture*) &(*dst), buffer, PIX_FMT_YUV420P,
        m_video_ctx->width, m_video_ctx->height);

    if (m_swsctx == NULL) {
        m_swsctx = sws_getContext(m_video_ctx->width,
                    m_video_ctx->height, m_video_ctx->pix_fmt,
                    m_video_ctx->width, m_video_ctx->height,
                    PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    }

    sws_scale(m_swsctx, src->data, src->linesize, 0,
                m_video_ctx->height, dst->data, dst->linesize);
}

void* VideoPlayer::dec_thrd_entry(void* param)
{
    VideoPlayer* player = (VideoPlayer*)param;
    player->dec_thrd();

    return NULL;
}

void* VideoPlayer::render_thrd_entry(void* param)
{
    VideoPlayer* player = (VideoPlayer*)param;
    player->render_thrd();

    return NULL;
}