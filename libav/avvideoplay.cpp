#include "avvideoplay.h"
#include "avplay.h"

extern AVPacket flush_pkt;
extern AVFrame flush_frm;

static void* video_dec_thrd(void *param);

static void* video_render_thrd(void *param);

/* 视频帧复制. */
static void video_copy(avplay *play, AVFrame *dst, AVFrame *src);

/* 更新视频pts. */
static void update_video_pts(avplay *play, double pts, int64_t pos);

avvideoplay* avvideoplay_create(avplay* play, AVCodecContext* ctx)
{
    avvideoplay* video = (avvideoplay*)malloc(sizeof(avvideoplay));
    if (video != NULL) {
        memset(video, 0, sizeof(*video));
        video->m_play = play;
        video->m_video_ctx = ctx;
    }

    return video;
}

int avvideoplay_start(avvideoplay* video)
{
    pthread_attr_t attr;
    int ret = 0;

    ret = open_decoder(video->m_video_ctx);
    if (ret != 0) {
        return ret;
    }

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    ret = pthread_create(&video->m_video_dec_thrd, &attr, video_dec_thrd,(void*) video->m_play);
    if (ret)
    {
        printf("ERROR; return code from pthread_create() is %d\n", ret);
        return ret;
    }

    ret = pthread_create(&video->m_video_render_thrd, &attr, video_render_thrd, (void*) video->m_play);
    if (ret)
    {
        printf("ERROR; return code from pthread_create() is %d\n", ret);
        return ret;
    }

    return ret;
}

int avvideoplay_stop(avvideoplay* video)
{
    void *status = NULL;

    video->m_abort = true;

    pthread_join(video->m_video_dec_thrd, &status);
    pthread_join(video->m_video_render_thrd, &status);

    if (video->m_swsctx != NULL) {
        sws_freeContext(video->m_swsctx);
        video->m_swsctx = NULL;
    }

    if (video->m_video_ctx) {
        avcodec_close(video->m_video_ctx);
        video->m_video_ctx = NULL;
    }

    return 0;
}

void avvideoplay_destroy(avvideoplay* video)
{
    free(video);
}

double avvideoplay_clock(avvideoplay* video)
{
    avplay *play = video->m_play;
    if (play->m_play_status == paused)
        return video->m_video_current_pts;
    return video->m_video_current_pts_drift + av_gettime() / 1000000.0f;
}

static void update_video_pts(avplay *play, double pts, int64_t pos)
{
    avvideoplay* video = play->m_videoplay;
    double time = av_gettime() / 1000000.0;
    /* update current video pts */
    video->m_video_current_pts = pts;
    video->m_video_current_pts_drift = video->m_video_current_pts - time;
    video->m_video_current_pos = pos;
    video->m_frame_last_pts = pts;
}

static void video_copy(avplay *play, AVFrame *dst, AVFrame *src)
{
    avvideoplay* video = play->m_videoplay;
    uint8_t *buffer;
    int len = avpicture_get_size(PIX_FMT_YUV420P, video->m_video_ctx->width,
        video->m_video_ctx->height);
    *dst = *src;
    buffer = (uint8_t*) av_malloc(len);
    assert(buffer);

    avpicture_fill((AVPicture*) &(*dst), buffer, PIX_FMT_YUV420P,
        video->m_video_ctx->width, video->m_video_ctx->height);

    if (video->m_swsctx == NULL) {
        video->m_swsctx = sws_getContext(video->m_video_ctx->width,
            video->m_video_ctx->height, video->m_video_ctx->pix_fmt,
            video->m_video_ctx->width, video->m_video_ctx->height,
            PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    }

    sws_scale(video->m_swsctx, src->data, src->linesize, 0,
        video->m_video_ctx->height, dst->data, dst->linesize);
}

static void* video_dec_thrd(void *param)
{
    AVPacket pkt, pkt2;
    AVFrame *avframe, avcopy;
    int got_picture = 0;
    int ret = 0;
    avplay *play = (avplay*) param;
    int64_t v_start_time = 0;
    int64_t a_start_time = 0;
    avvideoplay* video = play->m_videoplay;

    avframe = avcodec_alloc_frame();

    if (play->m_video_st && play->m_audio_st)
    {
        v_start_time = play->m_video_st->start_time;
        a_start_time = play->m_audio_st->start_time;
    }

    for (; !play->m_abort;)
    {
        av_init_packet(&pkt);
        while (play->m_play_status == paused && !play->m_abort)
            Sleep(10);
        ret = get_queue(&play->m_video_q, (AVPacket*) &pkt);
        if (ret != -1)
        {
            if (pkt.data == flush_pkt.data)
            {
                AVFrameList* lst = NULL;

                avcodec_flush_buffers(video->m_video_ctx);

                while (play->m_video_dq.m_size && !play->m_abort)
                    Sleep(1);

                pthread_mutex_lock(&play->m_video_dq.m_mutex);
                lst = (AVFrameList*)play->m_video_dq.m_first_pkt;
                for (; lst != NULL; lst = lst->next)
                    lst->pkt.type = 1; /* type为1表示skip. */
                video->m_video_current_pos = -1;
                video->m_frame_last_dropped_pts = AV_NOPTS_VALUE;
                video->m_frame_last_duration = 0;
                video->m_frame_timer = (double) av_gettime() / 1000000.0f;
                video->m_video_current_pts_drift = -video->m_frame_timer;
                video->m_frame_last_pts = AV_NOPTS_VALUE;
                pthread_mutex_unlock(&play->m_video_dq.m_mutex);

                continue;
            }

            pthread_mutex_lock(&play->m_buf_size_mtx);
            play->m_pkt_buffer_size -= pkt.size;
            pthread_mutex_unlock(&play->m_buf_size_mtx);
            pkt2 = pkt;

            while (pkt2.size > 0 && !play->m_abort)
            {
                ret = avcodec_decode_video2(video->m_video_ctx, avframe, &got_picture, &pkt2);
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

                video_copy(play, &avcopy, avframe);

                /*
                * 初始化m_frame_timer时间, 使用系统时间.
                */
                if (video->m_frame_timer == 0.0f)
                    video->m_frame_timer = (double) av_gettime() / 1000000.0f;

                /*
                * 计算pts值.
                */
                pts1 = (avcopy.best_effort_timestamp - a_start_time) * av_q2d(play->m_video_st->time_base);
                if (pts1 == AV_NOPTS_VALUE)
                    pts1 = 0;
                pts = pts1;

                /* 如果以音频同步为主, 则在此判断是否进行丢包. */
                if ((play->m_audio_st) &&
                    ((play->m_av_sync_type == AV_SYNC_AUDIO_MASTER && play->m_audio_st)
                    || play->m_av_sync_type == AV_SYNC_EXTERNAL_CLOCK))
                {
                    pthread_mutex_lock(&play->m_video_dq.m_mutex);
                    /*
                    * 最后帧的pts是否为AV_NOPTS_VALUE 且 pts不等于0
                    * 计算视频时钟和主时钟源的时间差.
                    * 计算pts时间差, 当前pts和上一帧的pts差值.
                    */
                    ret = 1;
                    if (video->m_frame_last_pts != AV_NOPTS_VALUE && pts)
                    {
                        double clockdiff = avvideoplay_clock(play->m_videoplay) - master_clock(play);
                        double ptsdiff = pts - video->m_frame_last_pts;

                        /*
                        * 如果clockdiff和ptsdiff同时都在同步阀值范围内
                        * 并且clockdiff与ptsdiff之和与m_frame_last_filter_delay的差
                        * 如果小于0, 则丢弃这个视频帧.
                        */
                        if (fabs(clockdiff) < AV_NOSYNC_THRESHOLD && ptsdiff > 0
                            && ptsdiff < AV_NOSYNC_THRESHOLD
                            && clockdiff + ptsdiff - video->m_frame_last_filter_delay < 0)
                        {
                            video->m_frame_last_dropped_pos = pkt.pos;
                            video->m_frame_last_dropped_pts = pts;
                            video->m_drop_frame_num++;
                            printf("\nDROP: %3d drop a frame of pts is: %.3f\n", video->m_drop_frame_num, pts);
                            ret = 0;
                        }
                    }
                    pthread_mutex_unlock(&play->m_video_dq.m_mutex);
                    if (ret == 0)
                    {
                        /* 丢掉该帧. */
                        av_free(avcopy.data[0]);
                        continue;
                    }
                }

                /* 计录最后有效帧时间. */
                video->m_frame_last_returned_time = av_gettime() / 1000000.0f;
                /* m_frame_last_filter_delay基本都是0吧. */
                video->m_frame_last_filter_delay = av_gettime() / 1000000.0f
                    - video->m_frame_last_returned_time;
                /* 如果m_frame_last_filter_delay还可能大于1, 那么m_frame_last_filter_delay置0. */
                if (fabs(video->m_frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0f)
                    video->m_frame_last_filter_delay = 0.0f;

                /*
                *	更新当前m_video_clock为当前解码pts.
                */
                if (pts != 0)
                    video->m_video_clock = pts;
                else
                    pts = video->m_video_clock;

                /*
                *	计算当前帧的延迟时长.
                */
                frame_delay = av_q2d(video->m_video_ctx->time_base);
                frame_delay += avcopy.repeat_pict * (frame_delay * 0.5);

                /*
                * m_video_clock加上该帧延迟时长,
                * m_video_clock是估算出来的下一帧的pts.
                */
                video->m_video_clock += frame_delay;

                /*
                * 防止内存过大.
                */
                chk_queue(&play->m_video_dq, AVDECODE_BUFFER_SIZE);

                /* 保存frame_delay为该帧的duration, 保存到.pts字段中. */
                memcpy(&avcopy.pkt_dts, &frame_delay, sizeof(double));
                /* 保存pts. */
                memcpy(&avcopy.pts, &pts, sizeof(double));
                /* 保存pos, pos即是文件位置. */
                avcopy.pkt_pos = pkt.pos;
                /* type为0表示no skip. */
                avcopy.type = 0;

                /* 丢进播放队列.	*/
                put_queue(&play->m_video_dq, &avcopy);
            }
            av_free_packet(&pkt);
        }
    }
    av_free(avframe);
    return NULL;
}

static void* video_render_thrd(void *param)
{
    avplay *play = (avplay*) param;
    avvideoplay* video = play->m_videoplay;
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

    while (!play->m_abort)
    {
        /* 如果视频队列为空 */
        if (play->m_video_dq.m_size == 0)
        {
            pthread_mutex_lock(&play->m_video_dq.m_mutex);
            /*
            * 如果最后丢弃帧的pts不为空, 且大于最后pts则
            * 使用最后丢弃帧的pts值更新其它相关的pts值.
            */
            if (video->m_frame_last_dropped_pts != AV_NOPTS_VALUE && video->m_frame_last_dropped_pts > video->m_frame_last_pts)
            {
                update_video_pts(play, video->m_frame_last_dropped_pts, video->m_frame_last_dropped_pos);
                video->m_frame_last_dropped_pts = AV_NOPTS_VALUE;
            }
            pthread_mutex_unlock(&play->m_video_dq.m_mutex);
        }
        /* 获得下一帧视频. */
        ret = get_queue(&play->m_video_dq, &video_frame);
        if (ret != -1)
        {
            // 状态为正在渲染.
            play->m_rendering = 1;
            // 如果没有初始化渲染器, 则初始化渲染器.
            if (!inited && play->m_vo_ctx)
            {
                inited = 1;
                play->m_vo_ctx->fps = (float)play->m_video_st->r_frame_rate.num / (float)play->m_video_st->r_frame_rate.den;
                ret = play->m_vo_ctx->init_video(play->m_vo_ctx,
                    video->m_video_ctx->width, video->m_video_ctx->height, video->m_video_ctx->pix_fmt);
                if (ret != 0)
                    inited = -1;
                else
                    play->m_play_status = playing;
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
                last_duration = current_pts - video->m_frame_last_pts;
                if (last_duration > 0 && last_duration < 10.0)
                {
                    /* 更新m_frame_last_duration. */
                    video->m_frame_last_duration = last_duration;
                }

                /* 更新延迟同步到主时钟源. */
                delay = video->m_frame_last_duration;
                if ((play->m_av_sync_type == AV_SYNC_EXTERNAL_CLOCK) ||
                    (play->m_av_sync_type == AV_SYNC_AUDIO_MASTER && play->m_audio_st))
                {
                    diff = avvideoplay_clock(play->m_videoplay) - master_clock(play);
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
                if (time < video->m_frame_timer + delay)
                {
                    Sleep(1);
                    continue;
                }

                /* 更新m_frame_timer. */
                if (delay > 0.0f)
                    video->m_frame_timer += delay * FFMAX(1, floor((time - video->m_frame_timer) / delay));

                pthread_mutex_lock(&play->m_video_dq.m_mutex);
                update_video_pts(play, current_pts, video_frame.pkt_pos);
                pthread_mutex_unlock(&play->m_video_dq.m_mutex);

                /* 计算下一帧的时间.  */
                if (play->m_video_dq.m_size > 0)
                {
                    memcpy(&next_pts, &(((AVFrameList*) play->m_video_dq.m_last_pkt)->pkt.pts), sizeof(double));
                    duration = next_pts - current_pts;
                }
                else
                {
                    memcpy(&duration, &video_frame.pkt_dts, sizeof(double));
                }

                if (play->m_audio_st && time > video->m_frame_timer + duration)
                {
                    if (play->m_video_dq.m_size > 1)
                    {
                        pthread_mutex_lock(&play->m_video_dq.m_mutex);
                        video->m_drop_frame_num++;
                        pthread_mutex_unlock(&play->m_video_dq.m_mutex);
                        printf("\nDROP: %3d drop a frame of pts is: %.3f\n", video->m_drop_frame_num, current_pts);
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
                if (video->m_enable_calc_frame_rate)
                {
                    int current_time = 0;
                    /* 计算时间是否足够一秒钟. */
                    if (video->m_last_fr_time == 0)
                        video->m_last_fr_time = av_gettime() / 1000000.0f;
                    current_time = av_gettime() / 1000000.0f;
                    if (current_time - video->m_last_fr_time >= 1)
                    {
                        video->m_last_fr_time = current_time;
                        if (++video->m_fr_index == MAX_CALC_SEC)
                            video->m_fr_index = 0;

                        /* 计算frame_rate. */
                        do
                        {
                            int sum = 0;
                            int i = 0;
                            for (; i < MAX_CALC_SEC; i++)
                                sum += video->m_frame_num[i];
                            video->m_real_frame_rate = (double)sum / (double)MAX_CALC_SEC;
                        } while (0);
                        /* 清空. */
                        video->m_frame_num[video->m_fr_index] = 0;
                    }

                    /* 更新读取字节数. */
                    video->m_frame_num[video->m_fr_index]++;
                }

                /* 已经开始播放, 清空seeking的状态. */
                if (play->m_seeking == SEEKING_FLAG)
                    play->m_seeking = NOSEEKING_FLAG;

                if (inited == 1 && play->m_vo_ctx)
                {
                    play->m_vo_ctx->render_one_frame(play->m_vo_ctx, &video_frame, video->m_video_ctx->pix_fmt, av_curr_play_time(play));
                    if (delay != 0)
                        Sleep(4);
                }
                break;
            } while (TRUE);

            /* 渲染完成. */
            play->m_rendering = 0;

            /* 如果处于暂停状态, 则直接渲染窗口, 以免黑屏. */
            while (play->m_play_status == paused && inited == 1 && play->m_vo_ctx && !play->m_abort)
            {
                play->m_vo_ctx->render_one_frame(play->m_vo_ctx, &video_frame, video->m_video_ctx->pix_fmt, av_curr_play_time(play));
                Sleep(16);
            }

            /* 释放视频帧缓冲. */
            av_free(video_frame.data[0]);
        }
    }
    return NULL;
}
