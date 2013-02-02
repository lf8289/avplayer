#include <stdlib.h>
#include <math.h>
#include "avplay.h"
#include "mediaplayer.h"

source_context* alloc_media_source(int type, const char *data, int len, int64_t size)
{
    struct source_context *ptr = (struct source_context *)malloc(sizeof(source_context));

    /* 清空结构体. */
    memset(ptr, 0, sizeof(source_context));

    /* 参数赋值. */
    ptr->type = type;

    if (type != MEDIA_TYPE_BT)
    {
        /* 保存文件路径或url信息. */
        ptr->media = (media_info *)calloc(1, sizeof(media_info));
        ptr->media->start_pos = 0;
        ptr->media->file_size = size;

        /* 保存文件名. */
        ptr->media->name = strdup(data);

        /*
        * 媒体文件数为1, 只有打开bt种子时, bt中存在多个视频文件,
        * media_size才可能大于1.
        */
        ptr->media_size = 1;
    }
    else
    {
        /* 保存种子文件数据. */
        ptr->torrent_data = (char*)calloc(1, len);
        memcpy(ptr->torrent_data, data, len);
        ptr->torrent_len = len;
    }

    return ptr;
}

void free_media_source(source_context *ctx)
{
    int i = 0;

    /* 释放io对象. */
    if (ctx->io_dev)
        ctx->destory(ctx);

    /* 释放data. */
    if (ctx->torrent_data)
        free(ctx->torrent_data);

    /* 释放media_info. */
    for (; i < ctx->media_size; i++)
    {
        if (ctx->media[i].name)
            free(ctx->media[i].name);
    }
    if (ctx->media)
        free(ctx->media);

    /* 最后释放整个source_context. */
    free(ctx);
}

ao_context* alloc_audio_render()
{
    ao_context *ptr = (ao_context *)malloc(sizeof(ao_context));
    memset(ptr, 0, sizeof(ao_context));
    return ptr;
}

void free_audio_render(ao_context *ctx)
{
    if (ctx->audio_dev)
        ctx->destory_audio(ctx);
    free(ctx);
}

vo_context* alloc_video_render(void *user_data)
{
    struct vo_context *ptr = (struct vo_context *)malloc(sizeof(vo_context));
    memset(ptr, 0, sizeof(vo_context));
    ptr->user_data = user_data;
    return ptr;
}

void free_video_render(vo_context *ctx)
{
    if (ctx->video_dev)
        ctx->destory_video(ctx);
    free(ctx);
}

avplay* alloc_avplay_context()
{
    avplay* play = (avplay*)malloc(sizeof(avplay));
    memset(play, 0, sizeof(*play));
    play->player = new VMPlayer();

    return play;
}

void free_avplay_context(avplay *ctx)
{
    if (ctx->m_ao_ctx)
    {
        free_audio_render(ctx->m_ao_ctx);
        ctx->m_ao_ctx = NULL;
    }
    if (ctx->m_vo_ctx) {
        free_video_render(ctx->m_vo_ctx);
        ctx->m_vo_ctx = NULL;
    }
    if (ctx->m_source_ctx) {
        free_media_source(ctx->m_source_ctx);
        ctx->m_source_ctx = NULL;
    }

    delete ctx->player;
    free(ctx);
}

int initialize(avplay *play, source_context *sc)
{
    return play->player->initialize(sc);
}

void configure(avplay *play, void *param, int type)
{
    if (type == AUDIO_RENDER)
    {
        if (play->m_ao_ctx)
        {
            free_audio_render(play->m_ao_ctx);
            play->m_ao_ctx = NULL;
        }
        play->m_ao_ctx = (ao_context*)param;
    }
    if (type == VIDEO_RENDER)
    {
        if (play->m_vo_ctx) {
            free_video_render(play->m_vo_ctx);
            play->m_vo_ctx = NULL;
        }
        play->m_vo_ctx = (vo_context*)param;
    }
    if (type == MEDIA_SOURCE)
    {
        if (play->m_source_ctx)
        {
            play->m_source_ctx->close(play->m_source_ctx);
            free_media_source(play->m_source_ctx);
        }
         play->m_source_ctx = (source_context*)param;
    }
    play->player->configure(param, type);
}

int av_start(avplay *play, double fact, int index)
{
    return play->player->av_start(fact, index);
}

void wait_for_completion(avplay *play)
{
    play->player->wait_for_completion();
}

void av_stop(avplay *play)
{
    play->player->av_stop();
}

void av_pause(avplay *play)
{
    play->player->av_pause();
}

void av_resume(avplay *play)
{
    play->player->av_resume();
}

void av_seek(avplay *play, double fact)
{
    play->player->av_seek(fact);
}

int av_volume(avplay *play, double l, double r)
{
    return play->player->av_volume(l, r);
}

int audio_is_inited(avplay *play)
{
    return play->player->audio_is_inited();
}

void av_mute_set(avplay *play, int s)
{
    play->player->av_mute_set(s);
}

double av_curr_play_time(avplay *play)
{
    return play->player->av_curr_play_time();
}

double av_duration(avplay *play)
{
    return play->player->av_duration();
}

play_status av_status(avplay* play)
{
    return play->player->get_status();
}

int av_width(avplay* play)
{
    return play->player->width();
}

int av_height(avplay* play)
{
    return play->player->height();
}

void av_destory(avplay *play)
{
    //TODO:
}

void enable_calc_frame_rate(avplay *play)
{
    play->player->enable_calc_frame_rate();
}

void enable_calc_bit_rate(avplay *play)
{
    play->player->enable_calc_bit_rate();
}

int current_bit_rate(avplay *play)
{
    return play->player->current_bit_rate();
}

int current_frame_rate(avplay *play)
{
    return play->player->current_frame_rate();
}

double buffering(avplay *play)
{
    return play->player->buffering();
}

//---------------------------------------------------------------------------------------

