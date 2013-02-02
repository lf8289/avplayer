/*
 * mediaplayer.cpp
 */

//#define LOG_NDEBUG 0
#define TAG "FFMpegMediaPlayer"

#include <sys/types.h>
//#include <sys/time.h>
#include <sys/stat.h>
//#include <unistd.h>
#include <fcntl.h>

extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h"

} // end of extern C

#include "mediaplayer.h"


#include "avvideoplay.h"
#include "avaudioplay.h"

#define IO_BUFFER_SIZE			32768
#define MAX_PKT_BUFFER_SIZE		5242880

/* INT64最大最小取值范围. */
#ifndef INT64_MIN
#define INT64_MIN (-9223372036854775807LL - 1)
#endif
#ifndef INT64_MAX
#define INT64_MAX (9223372036854775807LL)
#endif

VMPlayer::VMPlayer()
: m_format_ctx(NULL)
, m_audio_st(NULL)
, m_video_st(NULL)
, m_audio_index(0)
, m_video_index(0)
, m_pkt_buffer_size(0)
, m_av_sync_type(0)
, m_read_pause_return(0)
, m_seek_req(0)
, m_seek_flags(0)
, m_seek_pos(0)
, m_seek_rel(0)
, m_seek_by_bytes(0)
, m_seeking(0)
, m_external_clock(0)
, m_external_clock_time(0)
, m_source_ctx(NULL)
, m_avio_ctx(NULL)
, m_io_buffer(NULL)
, m_ao_inited(0)
, m_play_status(inited)
, m_enable_calc_video_bite(0)
, m_real_bit_rate(0)
, m_last_vb_time(0)
, m_vb_index(0)
, m_current_play_index(0)
, m_start_time(0)
, m_buffering(0)
, m_abort(0)
, audio_player_(NULL)
, video_player_(NULL)
{
    av_register_all();
    avcodec_register_all();
    avformat_network_init();
}

VMPlayer::~VMPlayer()
{
    /* 如果正在播放, 则关闭播放. */
    if (m_play_status != stoped && m_play_status != inited)
    {
        /* 关闭数据源. */
        if (m_source_ctx && m_source_ctx->io_dev)
            m_source_ctx->close(m_source_ctx);
        if (m_source_ctx && m_source_ctx->save_path)
            free(m_source_ctx->save_path);
        av_stop();
    }

    delete audio_player_;
    audio_player_ = NULL;
    delete video_player_;
    video_player_ = NULL;

    avformat_network_deinit();
}

int VMPlayer::initialize(source_context *sc)
{
    int ret = 0, i = 0;
    AVInputFormat *iformat = NULL;

    /* 分配一个format context. */
    m_format_ctx = avformat_alloc_context();
    m_format_ctx->flags = AVFMT_FLAG_GENPTS;
    m_format_ctx->interrupt_callback.callback = decode_interrupt_cb;
    m_format_ctx->interrupt_callback.opaque = this;

    /* 保存media_source指针并初始化, 由avplay负责管理释放其内存. */
    m_source_ctx = sc;

    /* 初始化数据源. */
    if (m_source_ctx->init_source &&
        m_source_ctx->init_source(sc) < 0)
    {
        return -1;
    }

    /* 获得媒体文件列表. */
    if (sc->type == MEDIA_TYPE_BT)
    {
        char name[2048] = {0};
        int64_t size = 2048;
        int64_t pos = 0;
        int i = 0;
        media_info *media = NULL;

        for (i = 0; ; i++)
        {
            int ret = m_source_ctx->bt_media_info(m_source_ctx, name, &pos, &size);
            if (ret == -1)
                break;
            if (i == 0)
            {
                m_source_ctx->media = (media_info *)malloc(sizeof(media_info) * ret);
                m_source_ctx->media_size = ret;
                media = m_source_ctx->media;
            }
            media[i].file_size = size;
            media[i].start_pos = pos;
            media[i].name = strdup(name);
            pos = i + 1;
            size = 2048;
        }
    }

    if (sc->type == MEDIA_TYPE_BT || sc->type == MEDIA_TYPE_FILE)
    {
        /* 分配用于io的缓冲. */
        m_io_buffer = (unsigned char*)av_malloc(IO_BUFFER_SIZE);
        if (!m_io_buffer)
        {
            printf("Create buffer failed!\n");
            return -1;
        }

        /* 分配io上下文. */
        m_avio_ctx = avio_alloc_context(m_io_buffer,
            IO_BUFFER_SIZE, 0, (void*)this, read_packet, NULL, seek_packet);
        if (!m_io_buffer)
        {
            printf("Create io context failed!\n");
            av_free(m_io_buffer);
            return -1;
        }
        m_avio_ctx->write_flag = 0;

        ret = av_probe_input_buffer(m_avio_ctx, &iformat, "", NULL, 0, 0);
        if (ret < 0)
        {
            printf("av_probe_input_buffer call failed!\n");
            goto FAILED_FLG;
        }

        /* 打开输入媒体流.	*/
        m_format_ctx->pb = m_avio_ctx;
        ret = avformat_open_input(&m_format_ctx, "", iformat, NULL);
        if (ret < 0)
        {
            printf("av_open_input_stream call failed!\n");
            goto FAILED_FLG;
        }
    }
    else
    {
        /* 判断url是否为空. */
        if (!m_source_ctx->media->name || !m_source_ctx->media_size)
            goto FAILED_FLG;

        /* HTTP和RTSP直接使用ffmpeg来处理.	*/
        ret = avformat_open_input(&m_format_ctx,
            m_source_ctx->media->name, iformat, NULL);
        if (ret < 0)
        {
            printf("av_open_input_stream call failed!\n");
            goto FAILED_FLG;
        }
    }

    ret = avformat_find_stream_info(m_format_ctx, NULL);
    if (ret < 0)
        goto FAILED_FLG;

    av_dump_format(m_format_ctx, 0, NULL, 0);

    /* 得到audio和video在streams中的index.	*/
    m_video_index =
        stream_index(AVMEDIA_TYPE_VIDEO, m_format_ctx);
    m_audio_index =
        stream_index(AVMEDIA_TYPE_AUDIO, m_format_ctx);
    if (m_video_index == -1 && m_audio_index == -1)
        goto FAILED_FLG;

    /* 保存audio和video的AVStream指针.	*/
    if (m_video_index != -1)
        m_video_st = m_format_ctx->streams[m_video_index];
    if (m_audio_index != -1)
        m_audio_st = m_format_ctx->streams[m_audio_index];

    /* 默认同步到音频.	*/
    m_av_sync_type = AV_SYNC_AUDIO_MASTER;
    m_abort = true;

    /* 初始化各变量. */
    av_init_packet(&flush_pkt);
    flush_pkt.data = (uint8_t *)"FLUSH";
    flush_frm.data[0] = (uint8_t *)"FLUSH";
    m_abort = 0;

    /* 初始化队列. */
    if (m_audio_index != -1)
    {
        /* 创建audio play*/
        audio_player_ = new AudioPlayer(this, m_format_ctx->streams[m_audio_index]);
        if (audio_player_ == NULL) {
            goto FAILED_FLG;
        }
    }
    if (m_video_index != -1)
    {
        /* 创建video play*/
        video_player_ = new VideoPlayer(this, m_format_ctx->streams[m_video_index]);
        if (video_player_ == NULL) {
            goto FAILED_FLG;
        }
    }

    /* 初始化读取文件数据缓冲计数mutex. */
    pthread_mutex_init(&m_buf_size_mtx, NULL);

    /* 打开各线程.	*/
    return 0;

FAILED_FLG:
    if (m_format_ctx)
        avformat_close_input(&m_format_ctx);
    if (m_avio_ctx)
        av_free(m_avio_ctx);
    if (m_io_buffer)
        av_free(m_io_buffer);
    if (video_player_) {
        delete video_player_;
        video_player_ = NULL;
    }
    if (audio_player_ != NULL) {
        delete audio_player_;
        audio_player_ = NULL;
    }

    return -1;
}

void VMPlayer::configure(void *param, int type)
{
    if (type == AUDIO_RENDER)
    {
        audio_player_->set_render((ao_context*)param);
    }
    if (type == VIDEO_RENDER)
    {
        video_player_->set_render((vo_context*)param);
    }
    if (type == MEDIA_SOURCE)
    {
        /* 注意如果正在播放, 则不可以配置应该源. */
        if (m_play_status == playing ||
            m_play_status == paused)
            return ;
        m_source_ctx = (source_context*)param;
    }
}

int VMPlayer::av_start(double fact, int index)
{
    pthread_attr_t attr;
    int ret;

    /* 保存正在播放的索引号. */
    m_current_play_index = index;
    if (index > m_source_ctx->media_size)
        return -1;
    /* 保存起始播放时间. */
    m_start_time = fact;

    if (m_audio_index != -1)
    {
        ret = audio_player_->start();
        if (ret)
        {
            printf("ERROR; return code from pthread_create() is %d\n", ret);
            return ret;
        }
    }
    if (m_video_index != -1)
    {
        ret = video_player_->start();
        if (ret)
        {
            printf("ERROR; return code from pthread_create() is %d\n", ret);
            return ret;
        }
    }

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* 创建线程. */
    ret = pthread_create(&m_read_pkt_thrd, &attr, read_pkt_thrd_entry, (void*)this);
    if (ret)
    {
        printf("ERROR; return code from pthread_create() is %d\n", ret);
        return ret;
    }

    m_play_status = playing;

    return 0;
}

void VMPlayer::wait_for_completion()
{
    while (m_play_status == playing ||
        m_play_status == paused)
    {
        Sleep(100);
    }
}

void VMPlayer::av_stop()
{
    m_abort = TRUE;
    if (m_source_ctx)
        m_source_ctx->abort = TRUE;

    /* 通知各线程退出. */

    /* 先等线程退出, 再释放资源. */
    wait_for_threads();

    /* 关闭解码器以及渲染器. */
    if (m_format_ctx)
        avformat_close_input(&m_format_ctx);
#ifdef WIN32
    if (m_buf_size_mtx)
#endif
        pthread_mutex_destroy(&m_buf_size_mtx);
#if 0
    if (m_ao_ctx)
    {
        free_audio_render(m_ao_ctx);
        m_ao_ctx = NULL;
        m_ao_inited = 0;
    }

    if (m_vo_ctx)
    {
        free_video_render(m_vo_ctx);
        m_vo_ctx = NULL;
    }
#endif
    if (m_avio_ctx)
    {
        av_free(m_avio_ctx);
        m_avio_ctx = NULL;
    }
}

void VMPlayer::av_pause()
{
    /* 一直等待为渲染状态时才控制为暂停, 原因是这样可以在暂停时继续渲染而不至于黑屏. */
    while (!video_player_->is_rending())
        Sleep(0);
    /* 更改播放状态. */
    m_play_status = paused;
}

void VMPlayer::av_resume()
{
    /* 更改播放状态. */
    m_play_status = playing;
}

void VMPlayer::av_seek(double fact)
{
    double duration = (double)m_format_ctx->duration / AV_TIME_BASE;

    /* 正在seek中, 只保存当前sec, 在seek完成后, 再seek. */
    if (m_seeking == SEEKING_FLAG ||
        (m_seeking > NOSEEKING_FLAG && m_seek_req))
    {
        m_seeking = fact * 1000;
        return ;
    }

    /* 正常情况下的seek. */
    if (m_format_ctx->duration <= 0)
    {
        uint64_t size = avio_size(m_format_ctx->pb);
        if (!m_seek_req)
        {
            m_seek_req = 1;
            m_seeking = SEEKING_FLAG;
            m_seek_pos = fact * size;
            m_seek_rel = 0;
            m_seek_flags &= ~AVSEEK_FLAG_BYTE;
            m_seek_flags |= AVSEEK_FLAG_BYTE;
        }
    }
    else
    {
        if (!m_seek_req)
        {
            m_seek_req = 1;
            m_seeking = SEEKING_FLAG;
            m_seek_pos = fact * duration;
            m_seek_rel = 0;
            m_seek_flags &= ~AVSEEK_FLAG_BYTE;
            /* m_seek_flags |= AVSEEK_FLAG_BYTE; */
        }
    }
}

int VMPlayer::av_volume(double l, double r)
{
    return audio_player_->set_volume(l, r);
}

int VMPlayer::audio_is_inited()
{
    return m_ao_inited;
}

void VMPlayer::av_mute_set(int s)
{
    audio_player_->set_mute(s);
}

double VMPlayer::av_curr_play_time()
{
    return master_clock();
}

double VMPlayer::av_duration()
{
    return (double)m_format_ctx->duration / AV_TIME_BASE;
}

void VMPlayer::enable_calc_frame_rate()
{
    video_player_->enable_calc_frame_rate();
}

void VMPlayer::enable_calc_bit_rate()
{
    m_enable_calc_video_bite = 1;
}

int VMPlayer::current_bit_rate()
{
    return m_real_bit_rate;
}

int VMPlayer::current_frame_rate()
{
    return video_player_->get_frame_rate();
}

int VMPlayer::width()
{
    return m_video_st->codec->width;
}

int VMPlayer::height()
{
    return m_video_st->codec->height;
}

double VMPlayer::buffering()
{
    return m_buffering;
}

double VMPlayer::master_clock()
{
    double val;

    if (m_av_sync_type == AV_SYNC_VIDEO_MASTER)
    {
        if (m_video_st)
            video_player_->clock();
        else
            audio_player_->clock();
    }
    else if (m_av_sync_type == AV_SYNC_AUDIO_MASTER)
    {
        if (m_audio_st)
            val = audio_player_->clock();
        else
            val = video_player_->clock();
    }
    else
    {
        val = external_clock();
    }

    return val;
}

int VMPlayer::open_decoder(AVCodecContext *ctx)
{
    int ret = 0;
    AVCodec *codec = NULL;

    /* 查找解码器. */
    codec = avcodec_find_decoder(ctx->codec_id);
    if (!codec)
        return -1;

    /* 打开解码器.	*/
    ret = avcodec_open2(ctx, codec, NULL);
    if (ret != 0)
        return ret;

    return ret;
}

AVStream* VMPlayer::get_video_stream()
{
    return m_video_st;
}

AVStream* VMPlayer::get_audio_stream()
{
    return m_audio_st;
}

play_status VMPlayer::get_status()
{
    return m_play_status;
}

void VMPlayer::set_status(play_status status)
{
    m_play_status = status;
}

void VMPlayer::set_seek_status(int status)
{
    m_seeking = NOSEEKING_FLAG;
}

int VMPlayer::get_sync_type()
{
    return m_av_sync_type;
}

void VMPlayer::buffer_size_add(int diff)
{
    pthread_mutex_lock(&m_buf_size_mtx);
    m_pkt_buffer_size += diff;
    pthread_mutex_unlock(&m_buf_size_mtx);
}

void VMPlayer::wait_for_threads()
{
    void *status = NULL;
    pthread_join(m_read_pkt_thrd, &status);

    if (m_audio_index != -1) {
        audio_player_->stop();
    }
    if (m_video_index != -1) {
        video_player_->stop();
    }

    /* 更改播放状态. */
    m_play_status = stoped;
}

media_info* VMPlayer::find_media_info(source_context *sc, int index)
{
    media_info *mi = sc->media;
    int i = 0;
    if (index > sc->media_size)
        return NULL;
    for (; i < index; i++)
        mi++;

    return mi;
}

int VMPlayer::stream_index(enum AVMediaType type, AVFormatContext *ctx)
{
    unsigned int i;

    for (i = 0; (unsigned int) i < ctx->nb_streams; i++)
        if (ctx->streams[i]->codec->codec_type == type)
            return i;
    return -1;
}

double VMPlayer::external_clock()
{
    int64_t ti;
    ti = av_gettime();
    return m_external_clock + ((ti - m_external_clock_time) * 1e-6);
}

void VMPlayer::read_pkt_thrd()
{
    AVPacket packet = { 0 };
    int ret;
    int last_paused = m_play_status;
    AVStream *stream = NULL;

    // 起始时间不等于0, 则先seek至指定时间.
    if (fabs(m_start_time) > 1.0e-6)
    {
        av_seek(m_start_time);
    }

    m_buffering = 0.0f;
    m_real_bit_rate = 0;

    for (; !m_abort;)
    {
        /* Initialize optional fields of a packet with default values.	*/
        av_init_packet(&packet);

        /* 如果暂定状态改变. */
        if (last_paused != m_play_status)
        {
            last_paused = m_play_status;
            if (m_play_status == playing)
                av_read_play(m_format_ctx);
            if (m_play_status == paused)
                m_read_pause_return = av_read_pause(m_format_ctx);
        }

        /* 如果seek未完成又来了新的seek请求. */
        if (m_seeking > NOSEEKING_FLAG)
            av_seek((double)m_seeking / 1000.0f);

        if (m_seek_req)
        {
            int64_t seek_target = m_seek_pos * AV_TIME_BASE;
            int64_t seek_min    = /*play->m_seek_rel > 0 ? seek_target - play->m_seek_rel + 2:*/ INT64_MIN;
            int64_t seek_max    = /*play->m_seek_rel < 0 ? seek_target - play->m_seek_rel - 2:*/ INT64_MAX;
            int seek_flags = 0 & (~AVSEEK_FLAG_BYTE);
            int ns, hh, mm, ss;
            int tns, thh, tmm, tss;
            double frac = (double)m_seek_pos / ((double)m_format_ctx->duration / AV_TIME_BASE);

            tns = m_format_ctx->duration / AV_TIME_BASE;
            thh = tns / 3600.0f;
            tmm = (tns % 3600) / 60.0f;
            tss = tns % 60;

            ns = frac * tns;
            hh = ns / 3600.0f;
            mm = (ns % 3600) / 60.0f;
            ss = ns % 60;

            seek_target = frac * m_format_ctx->duration;
            if (m_format_ctx->start_time != AV_NOPTS_VALUE)
                seek_target += m_format_ctx->start_time;

            if (m_audio_index >= 0)
            {
                audio_player_->put_flush_packet();
            }
            if (m_video_index >= 0)
            {
                video_player_->put_flush_packet();
            }
            m_pkt_buffer_size = 0;

            ret = avformat_seek_file(m_format_ctx, -1, seek_min, seek_target, seek_max, seek_flags);
            if (ret < 0)
            {
                fprintf(stderr, "%s: error while seeking\n", m_format_ctx->filename);
            }

            printf("Seek to %2.0f%% (%02d:%02d:%02d) of total duration (%02d:%02d:%02d)\n",
                frac * 100, hh, mm, ss, thh, tmm, tss);

            m_seek_req = 0;
        }

        /* 缓冲读满, 在这休眠让出cpu.	*/
        while (m_pkt_buffer_size > MAX_PKT_BUFFER_SIZE && !m_abort && !m_seek_req)
            Sleep(32);
        if (m_abort)
            break;

        /* Return 0 if OK, < 0 on error or end of file.	*/
        ret = av_read_frame(m_format_ctx, &packet);
        if (ret < 0)
        {
#if 0
            if (queue_size(&m_video_q) == 0 &&
                queue_size(&m_audio_q) == 0 &&
                queue_size(&m_video_dq) == 0 &&
                queue_size(&m_audio_dq) == 0)
                m_play_status = completed;
#endif
            Sleep(100);
            continue;
        }

        if (m_play_status == completed)
            m_play_status = playing;

        /* 更新缓冲字节数.	*/
        if (packet.stream_index == m_video_index || packet.stream_index == m_audio_index)
        {
            pthread_mutex_lock(&m_buf_size_mtx);
            m_pkt_buffer_size += packet.size;
            m_buffering = (double)m_pkt_buffer_size / (double)MAX_PKT_BUFFER_SIZE;
            pthread_mutex_unlock(&m_buf_size_mtx);
        }

        /* 在这里计算码率.	*/
        if (m_enable_calc_video_bite)
        {
            int current_time = 0;
            /* 计算时间是否足够一秒钟. */
            if (m_last_vb_time == 0)
                m_last_vb_time = av_gettime() / 1000000.0f;
            current_time = av_gettime() / 1000000.0f;
            if (current_time - m_last_vb_time >= 1)
            {
                m_last_vb_time = current_time;
                if (++m_vb_index == MAX_CALC_SEC)
                    m_vb_index = 0;

                /* 计算bit/second. */
                do
                {
                    int sum = 0;
                    int i = 0;
                    for (; i < MAX_CALC_SEC; i++)
                        sum += m_read_bytes[i];
                    m_real_bit_rate = ((double)sum / (double)MAX_CALC_SEC) * 8.0f / 1024.0f;
                } while (0);
                /* 清空. */
                m_read_bytes[m_vb_index] = 0;
            }

            /* 更新读取字节数. */
            m_read_bytes[m_vb_index] += packet.size;
        }

        av_dup_packet(&packet);

        if (packet.stream_index == m_video_index)
            video_player_->put_packet(&packet);

        if (packet.stream_index == m_audio_index)
            audio_player_->put_packet(&packet);
    }

    /* 销毁media_source. */
    if (m_source_ctx)
    {
        if (m_source_ctx && m_source_ctx->io_dev)
            m_source_ctx->close(m_source_ctx);
        m_source_ctx = NULL;
    }

    /* 设置为退出状态.	*/
    m_abort = TRUE;
}

int VMPlayer::decode_interrupt_cb(void *ctx)
{
    VMPlayer* player = (VMPlayer*)ctx;
    return player->m_abort;
}

int64_t VMPlayer::seek_packet(void *opaque, int64_t offset, int whence)
{
    VMPlayer* player = (VMPlayer*)opaque;
    int64_t old_off = player->m_source_ctx->offset;

    if (player->m_abort)
        return -1;

    // 如果存在read_seek函数实现, 则调用相应的函数实现, 处理相关事件.
    if (player->m_source_ctx && player->m_source_ctx->read_seek)
    {
        player->m_source_ctx->read_seek(player->m_source_ctx, offset, whence);
    }

    // 在下面计算修改offset, 于是在read_data的时候, 通过参数offset提供指定位置.
    switch (whence)
    {
    case SEEK_SET:
        {
            media_info *mi = player->find_media_info(
                player->m_source_ctx, player->m_current_play_index);
            if (!mi)
                assert(0);
            /* 从当前文件的起始位置开始计算. */
            player->m_source_ctx->offset = mi->start_pos + offset;
        }
        break;
    case SEEK_CUR:
        /* 直接根据当前位置计算offset. */
        offset += player->m_source_ctx->offset;
        player->m_source_ctx->offset = offset;
        break;
    case SEEK_END:
        {
            int64_t size = 0;

            if (player->m_source_ctx->type != MEDIA_TYPE_BT)
            {
                size = player->m_source_ctx->media->file_size;
            }
            else if (player->m_source_ctx->type == MEDIA_TYPE_BT)
            {
                /* 找到正在播放的文件. */
                media_info *mi = player->find_media_info(player->m_source_ctx, player->m_current_play_index);
                if (mi)
                    size = mi->file_size;
                else
                    assert(0);
                /* 计算文件尾的位置. */
                size += mi->start_pos;
            }
            /* 计算当前位置. */
            offset += size;
            /* 保存当前位置. */
            player->m_source_ctx->offset = offset;
        }
        break;
    case AVSEEK_SIZE:
        {
            int64_t size = 0;

            if (player->m_source_ctx->type != MEDIA_TYPE_BT)
            {
                size = player->m_source_ctx->media->file_size;
            }
            else if (player->m_source_ctx->type == MEDIA_TYPE_BT)
            {
                /* 找到正在播放的文件. */
                media_info *mi = player->find_media_info(player->m_source_ctx, player->m_current_play_index);
                if (mi)
                    size = mi->file_size;
                else
                    assert(0);
                if (mi)
                    size = mi->file_size;
            }
            offset = size;
        }
        break;
        // 	case AVSEEK_FORCE:
        // 		{
        //
        // 		}
        // 		break;
    default:
        {
            assert(0);
        }
        break;
    }

    return offset;
}

int VMPlayer::read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    int read_bytes = 0;
    VMPlayer* player = (VMPlayer*)opaque;

    if (player->m_abort)
        return 0;
    read_bytes = player->m_source_ctx->read_data(player->m_source_ctx,
        (char*)buf, player->m_source_ctx->offset, buf_size);
    if (read_bytes == -1)
        return 0;
    player->m_source_ctx->offset += read_bytes;

    return read_bytes;
}

void* VMPlayer::read_pkt_thrd_entry(void* param)
{
    VMPlayer* player = (VMPlayer*)param;
    player->read_pkt_thrd();

    return NULL;
}
