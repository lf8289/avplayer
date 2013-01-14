#include <stdlib.h>
#include <math.h>
#include "avplay.h"
#include "avaudioplay.h"

extern AVPacket flush_pkt;
extern AVFrame flush_frm;

double audio_clock(avplay *play)
{
	double pts;
	int hw_buf_size, bytes_per_sec;
	pts = play->m_audio_clock;
	hw_buf_size = play->m_audio_buf_size - play->m_audio_buf_index;
	bytes_per_sec = 0;
	if (play->m_audio_st)
		bytes_per_sec = play->m_audio_st->codec->sample_rate * 2
		* FFMIN(play->m_audio_st->codec->channels, 2); /* 固定为2通道.	*/

	if (fabs(play->m_audio_current_pts_drift) <= 1.0e-6)
	{
		if (fabs(play->m_start_time) > 1.0e-6)
			play->m_audio_current_pts_drift = pts - play->m_audio_current_pts_last;
		else
			play->m_audio_current_pts_drift = pts;
	}

	if (bytes_per_sec)
		pts -= (double) hw_buf_size / bytes_per_sec;
	return pts - play->m_audio_current_pts_drift;
}

void audio_copy(avplay *play, AVFrame *dst, AVFrame* src)
{
	int nb_sample;
	int dst_buf_size;
	int out_channels;

	dst->linesize[0] = src->linesize[0];
	*dst = *src;
	dst->data[0] = NULL;
	dst->type = 0;
	out_channels = play->m_audio_ctx->channels;/* (FFMIN(play->m_audio_ctx->channels, 2)); */
	nb_sample = src->linesize[0] / play->m_audio_ctx->channels / av_get_bytes_per_sample(play->m_audio_ctx->sample_fmt);
	dst_buf_size = nb_sample * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * out_channels;
	dst->data[0] = (uint8_t*) av_malloc(dst_buf_size);
	assert(dst->data[0]);
	avcodec_fill_audio_frame(dst, out_channels, AV_SAMPLE_FMT_S16, dst->data[0], dst_buf_size, 0);

	/* 重采样到AV_SAMPLE_FMT_S16格式. */
	if (play->m_audio_ctx->sample_fmt != AV_SAMPLE_FMT_S16)
	{
		if (!play->m_swr_ctx)
		{
			uint64_t in_channel_layout = av_get_default_channel_layout(play->m_audio_ctx->channels);
			uint64_t out_channel_layout = av_get_default_channel_layout(out_channels);
			play->m_swr_ctx = swr_alloc_set_opts(NULL, in_channel_layout, AV_SAMPLE_FMT_S16,
				play->m_audio_ctx->sample_rate, in_channel_layout,
				play->m_audio_ctx->sample_fmt, play->m_audio_ctx->sample_rate, 0, NULL);
			swr_init(play->m_swr_ctx);
		}

		if (play->m_swr_ctx)
		{
			int ret, out_count;
			out_count = dst_buf_size / out_channels / av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
			ret = swr_convert(play->m_swr_ctx, dst->data, out_count, src->data, nb_sample);
			if (ret < 0)
				assert(0);
			src->linesize[0] = dst->linesize[0] = ret * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * out_channels;
			memcpy(src->data[0], dst->data[0], src->linesize[0]);
		}
	}

	/* 重采样到双声道. */
	if (play->m_audio_ctx->channels > 2)
	{
		if (!play->m_resample_ctx)
		{
			play->m_resample_ctx = av_audio_resample_init(
				FFMIN(2, play->m_audio_ctx->channels),
				play->m_audio_ctx->channels, play->m_audio_ctx->sample_rate,
				play->m_audio_ctx->sample_rate, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16,
				16, 10, 0, 0.f);
		}

		if (play->m_resample_ctx)
		{
			int samples = src->linesize[0] / (av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * play->m_audio_ctx->channels);
			dst->linesize[0] = audio_resample(play->m_resample_ctx,
				(short *) dst->data[0], (short *) src->data[0], samples) * 4;
		}
	}
	else
	{
		dst->linesize[0] = dst->linesize[0];
		memcpy(dst->data[0], src->data[0], dst->linesize[0]);
	}
}

void* audio_dec_thrd(void *param)
{
	AVPacket pkt, pkt2;
	int ret, n;
	AVFrame avframe = { 0 }, avcopy;
	avplay *play = (avplay*) param;
	int64_t v_start_time = 0;
	int64_t a_start_time = 0;

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
		ret = get_queue(&play->m_audio_q, &pkt);
		if (ret != -1)
		{
			if (pkt.data == flush_pkt.data)
			{
				AVFrameList* lst = NULL;
				avcodec_flush_buffers(play->m_audio_ctx);
				while (play->m_audio_dq.m_size && !play->m_abort)
					Sleep(1);
				pthread_mutex_lock(&play->m_audio_dq.m_mutex);
				lst = (AVFrameList*)play->m_audio_dq.m_first_pkt;
				for (; lst != NULL; lst = lst->next)
					lst->pkt.type = 1;	/*type为1表示skip.*/
				pthread_mutex_unlock(&play->m_audio_dq.m_mutex);
				continue;
			}

			/* 使用pts更新音频时钟. */
			if (pkt.pts != AV_NOPTS_VALUE)
				play->m_audio_clock = av_q2d(play->m_audio_st->time_base) * (pkt.pts - v_start_time);

			if (fabs(play->m_audio_current_pts_last) < 1.0e-6)
				play->m_audio_current_pts_last = play->m_audio_clock;

			/* 计算pkt缓冲数据大小. */
			pthread_mutex_lock(&play->m_buf_size_mtx);
			play->m_pkt_buffer_size -= pkt.size;
			pthread_mutex_unlock(&play->m_buf_size_mtx);

			/* 解码音频. */
			pkt2 = pkt;
			avcodec_get_frame_defaults(&avframe);

			while (!play->m_abort)
			{
				int got_frame = 0;
				ret = avcodec_decode_audio4(play->m_audio_ctx, &avframe, &got_frame, &pkt2);
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
					audio_copy(play, &avcopy, &avframe);

					/* 将计算的pts复制到avcopy.pts.  */
					memcpy(&avcopy.pts, &play->m_audio_clock, sizeof(double));

					/* 计算下一个audio的pts值.  */
					n = 2 * FFMIN(play->m_audio_ctx->channels, 2);

					play->m_audio_clock += ((double) avcopy.linesize[0] / (double) (n * play->m_audio_ctx->sample_rate));

					/* 如果不是以音频同步为主, 则需要计算是否移除一些采样以同步到其它方式.	*/
					if (play->m_av_sync_type == AV_SYNC_EXTERNAL_CLOCK ||
						play->m_av_sync_type == AV_SYNC_VIDEO_MASTER && play->m_video_st)
					{
						/* 暂无实现.	*/
					}

					/* 防止内存过大.	*/
					chk_queue(&play->m_audio_dq, AVDECODE_BUFFER_SIZE);

					/* 丢到播放队列中.	*/
					put_queue(&play->m_audio_dq, &avcopy);

					/* packet中数据已经没有数据了, 解码下一个音频packet. */
					if (pkt2.size <= 0)
						break;
				}
			}
			av_free_packet(&pkt);
		}
	}

	return NULL;
}

void* audio_render_thrd(void *param)
{
	avplay *play = (avplay*) param;
	AVFrame audio_frame;
	int audio_size = 0;
	int ret, temp, inited = 0;
	int bytes_per_sec;

	while (!play->m_abort)
	{
		ret = get_queue(&play->m_audio_dq, &audio_frame);
		if (audio_frame.data[0] == flush_frm.data[0])
			continue;
		if (ret != -1)
		{
			if (!inited && play->m_ao_ctx)
			{
				inited = 1;
				/* 配置渲染器. */
				ret = play->m_ao_ctx->init_audio(play->m_ao_ctx,
					FFMIN(play->m_audio_ctx->channels, 2), 16, play->m_audio_ctx->sample_rate, 0);
				if (ret != 0)
					inited = -1;
				else
				{
					/* 更改播放状态. */
					play->m_play_status = playing;
				}
				bytes_per_sec = play->m_audio_ctx->sample_rate *
					FFMIN(play->m_audio_ctx->channels, 2) * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
				/* 修改音频设备初始化状态, 置为1. */
				if (inited != -1)
					play->m_ao_inited = 1;
			}
			else if (!play->m_ao_ctx)
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
			play->m_audio_buf_size = audio_size;
			play->m_audio_buf_index = 0;

			/* 已经开始播放, 清空seeking的状态. */
			if (play->m_seeking == SEEKING_FLAG)
				play->m_seeking = NOSEEKING_FLAG;

			while (audio_size > 0)
			{
				if (inited == 1 && play->m_ao_ctx)
				{
					temp = play->m_ao_ctx->play_audio(play->m_ao_ctx,
						audio_frame.data[0] + play->m_audio_buf_index, play->m_audio_buf_size - play->m_audio_buf_index);
					play->m_audio_buf_index += temp;
					/* 如果缓冲已满, 则休眠一小会. */
					if (temp == 0)
					{
						if (play->m_audio_dq.m_size > 0)
						{
							if (((AVFrameList*) play->m_audio_dq.m_last_pkt)->pkt.type == 1)
								break;
						}
						Sleep(10);
					}
				}
				else
				{
					assert(0);
				}
				audio_size = play->m_audio_buf_size - play->m_audio_buf_index;
			}

			av_free(audio_frame.data[0]);
		}
	}
	return NULL;
}
