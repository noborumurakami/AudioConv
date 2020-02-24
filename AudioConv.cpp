// AudioConv.cpp : DLL アプリケーション用にエクスポートされる関数を定義します。
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavutil/avstring.h>
}

#include "ffStreamData.h"

#pragma warning(disable : 4996)

int gPcmBytes;
/***************************************
ffmpeg内からのexit()
***************************************/
void _sswEAExitProc(void)
{

}


#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

void Write16BitsLowHigh(FILE *fp, int i)
{
	putc(i & 0xff, fp);
	putc((i >> 8) & 0xff, fp);
}


void Write32BitsLowHigh(FILE *fp, int i)
{
	Write16BitsLowHigh(fp, (int)(i & 0xffffL));
	Write16BitsLowHigh(fp, (int)((i >> 16) & 0xffffL));
}

void Write24BitData(FILE *fp, unsigned long c)
{
	putc(c & 0xff, fp);
	putc((c >> 8) & 0xff, fp);
	putc((c >> 16) & 0xff, fp);
}


int Read16BitsHighLow(FILE *fp)
{
	int	first, second, result;

	first = 0xff & getc(fp);
	second = 0xff & getc(fp);

	result = (first << 8) + second;

	return(result);
}

int Read32BitsHighLow(FILE *fp)
{
	int	first, second, result;

	first = 0xffff & Read16BitsHighLow(fp);
	second = 0xffff & Read16BitsHighLow(fp);

	result = (first << 16) + second;
	return(result);
}

int Read16BitsLowHigh(FILE *fp)
{
	int	first, second, result;

	first = 0xff & getc(fp);
	second = 0xff & getc(fp);

	result = (second << 8) + first;
	return(result);
}


int Read32Bits(FILE *fp)
{
	int	first, second, result;

	first = 0xffff & Read16BitsLowHigh(fp);
	second = 0xffff & Read16BitsLowHigh(fp);

	result = (second << 16) + first;
	return result;
}

#define WAV_ID_RIFF 0x52494646 /* "RIFF" */
#define WAV_ID_WAVE 0x57415645 /* "WAVE" */
#define WAV_ID_FMT  0x666d7420 /* "fmt " */
#define WAV_ID_DATA 0x64617461 /* "data" */


int ReadPCMWaveHeader(FILE * fp, int *WaveFS, int *WaveCh, int *Bits, int *nDataByte)
{
	int format_tag = 0;
	int channels = 0;
	int block_align = 0;
	int bits_per_sample = 0;
	int samples_per_sec = 0;
	int avg_bytes_per_sec = 0;
	int data_length = 0;

	int type = Read32BitsHighLow(fp);
	if (type != WAV_ID_RIFF) {
		return 0;
	}

	int file_length = Read32Bits(fp);
	if (Read32BitsHighLow(fp) != WAV_ID_WAVE) {
		return 0;
	}
	int subSize;
	for (int i = 0; i < 20; i++) {
		type = Read32BitsHighLow(fp);

		if (type == WAV_ID_FMT) {
			subSize = Read32Bits(fp);
			if (subSize < 16) {
				return 0;
			}
			format_tag = Read16BitsLowHigh(fp);
			subSize -= 2;
			channels = Read16BitsLowHigh(fp);
			subSize -= 2;
			samples_per_sec = Read32Bits(fp);
			subSize -= 4;
			avg_bytes_per_sec = Read32Bits(fp);
			subSize -= 4;
			block_align = Read16BitsLowHigh(fp);
			subSize -= 2;
			bits_per_sample = Read16BitsLowHigh(fp);
			subSize -= 2;

			*WaveFS = samples_per_sec;
			*WaveCh = channels;
			*Bits = bits_per_sample;


			if (subSize > 0) {
				if (fseek(fp, subSize, SEEK_CUR) != 0) {
				return 0;
				}
			}
		}

		else if (type == WAV_ID_DATA) {
			subSize = Read32Bits(fp);
			*nDataByte = subSize;
			return 1;

		}
		else {
			subSize = Read32Bits(fp);
			if (fseek(fp, subSize, SEEK_CUR) != 0) {
				return 0;
			}
		}
	}
	return 0;

}

int GetWaveBuffer
(
	FILE * fp, 
	char *pWaveData[2], 
	int nSample, 
	int nCh, 
	int nBits
)
{
	if (nSample <= 0) return 0;
	int nReadSize = nSample * nCh * (nBits / 8);
	char *pReadBuf = new char[nReadSize];
	int data_size = fread(pReadBuf, 1, nReadSize, fp);
	float *pfOutBuf[2];
	pfOutBuf[0] = (float*)pWaveData[0];
	pfOutBuf[1] = (float*)pWaveData[1];
	int n = 0;
	short *p16 = (short *)pReadBuf;
	int *p32 = (int *)pReadBuf;
	char *p24 = (char *)pReadBuf;
	BYTE *p8 = (BYTE *)pReadBuf;
	for (int i = 0; i < nSample; i++) {
		for (int ch = 0; ch < nCh; ch++) {
			if (nBits == 8)  *pfOutBuf[ch]++ = (float)((int)*p8++ - 128) / (1 << (nBits - 1));
			else if (nBits == 16) {
				*pfOutBuf[ch]++ = (float)(*p16++) / (1 << (nBits - 1));
			}
			else if (nBits == 24) {
				int nWork = (int)((((BYTE)*p24 + (BYTE)*(p24 + 1) * 0x100 + (BYTE)*(p24 + 2) * 0x10000) << 8) / 256);
				double d = (double)nWork / (1 << (nBits - 1));
				*pfOutBuf[ch] = (float)d;
				if (*pfOutBuf[ch] > 1.0) {
					*pfOutBuf[ch] = 1.0;
				}
				if (*pfOutBuf[ch] < -1.0) {
					*pfOutBuf[ch] = -1.0;
				}
				p24 += 3;
				*pfOutBuf[ch]++;
			}
			else if (nBits == 32) {
				double d = (double)(*p32++) / (1 << (nBits - 1));
				*pfOutBuf[ch]++ = (float)d;
			}
		}

	}
	delete[] pReadBuf;
	return data_size / (nCh * (nBits / 8));

}

int WriteWaveHeader(FILE * fp, int pcmbytes,  int freq, int channels, int bits)
{
	int     bytes = (bits + 7) / 8;

	/* quick and dirty, but documented */
	fwrite("RIFF", 1, 4, fp); /* label */
	Write32BitsLowHigh(fp, pcmbytes + 44 - 8); /* length in bytes without header */
	fwrite("WAVEfmt ", 2, 4, fp); /* 2 labels */
	Write32BitsLowHigh(fp, 2 + 2 + 4 + 4 + 2 + 2); /* length of PCM format declaration area */
	Write16BitsLowHigh(fp, 1); /* is PCM? */
	Write16BitsLowHigh(fp, channels); /* number of channels */
	Write32BitsLowHigh(fp, freq); /* sample frequency in [Hz] */
	Write32BitsLowHigh(fp, freq * channels * bytes); /* bytes per second */
	Write16BitsLowHigh(fp, channels * bytes); /* bytes per sample time */
	Write16BitsLowHigh(fp, bits); /* bits per sample */
	fwrite("data", 1, 4, fp); /* label */
	Write32BitsLowHigh(fp, pcmbytes); /* length in bytes of raw PCM data */

	return ferror(fp) ? -1 : 0;
}

static bool decode
(	
	AVCodecContext *dec_ctx, 
	SwrContext *resample_ctx, 
	int output_channels, 
	int output_Bits, 
	int output_rate, 
	AVPacket *pkt, 
	AVFrame *frame,
	FILE *outfile, 
	int *ConverSamples
)
{
	int i, ch;
	int ret;
//	int a = AVERROR_EOF;
	/* send the packet with the compressed data to the decoder */
	ret = avcodec_send_packet(dec_ctx, pkt);
	if (ret < 0) {
	}

	int64_t dst_nb_samples;

	while (ret >= 0) {
		ret = avcodec_receive_frame(dec_ctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
		}
//		data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
//		if (data_size > 0) 
		{
	

			dst_nb_samples = av_rescale_rnd(swr_get_delay(resample_ctx, dec_ctx->sample_rate) +
				frame->nb_samples, output_rate, dec_ctx->sample_rate, AV_ROUND_UP);

			uint8_t **out_buffer;
			int dst_linesize;

			ret = av_samples_alloc_array_and_samples(&out_buffer, &dst_linesize, output_channels,
				dst_nb_samples, AV_SAMPLE_FMT_FLT, 0);

			int out_samples = swr_convert(resample_ctx, out_buffer, dst_nb_samples,
				(const uint8_t **)frame->data, frame->nb_samples);

			if (!out_samples) break;


			*ConverSamples += out_samples;
			char *pData = (char *)out_buffer[0];

			for (i = 0; i < out_samples; i++) {
				for (ch = 0; ch < output_channels; ch++) {
					float* f = (float *)pData;
					int32_t n = *f * (1 << (output_Bits - 1));
					if (output_Bits == 16) {
						if (n > 32767) n = 32767;
						if (n < -32768) n = -32768;

						Write16BitsLowHigh(outfile, n);
						gPcmBytes += 2;
					}
					else if (output_Bits == 24) {
						if (n > 8388607) n = 8388607;
						if (n < -8388608) n = -8388608;
						Write24BitData(outfile, n);
						gPcmBytes += 3;
					}
					else if (output_Bits == 32) {
						double d = *f;
						int64_t s64 = (int64_t)(d * (1 << (32 - 1)));
						if (s64 >INT_MAX) {
							s64 = INT_MAX;
						}
						if (s64 < INT_MIN) {
							s64 = INT_MIN;
						}
						n = (int32_t)s64;
						Write32BitsLowHigh(outfile, n);
						gPcmBytes += 4;
					}
					pData += 4;

				}
			}
			av_freep(&out_buffer);
		}
	}
	return TRUE;
}


int ConvertAACFile
(
	TCHAR *szIn, 
	TCHAR *szOut, 
	int nCh,			//-1 でオリジナル
	int nFS,			//-1 でオリジナル
	int nBits,
	int(CALLBACK*callbackFunction)(int, void*),
	void* pvUserData
)
{
	const TCHAR *outfilename, *filename;
	const AVCodec *codec;
	AVCodecContext *c = NULL;
	int ret;
	FILE *outfile;
	AVFrame *decoded_frame = NULL;

	filename = szIn;
	outfilename = szOut;

//	av_register_all();
//	avcodec_register_all();
//	avfilter_register_all();

	gPcmBytes = 0;

	atexit(_sswEAExitProc);		//ffmpeg内の関数はエラーが起きるととにかくexit()してしまうので、ここで捕まえる



	AVFormatContext *format_context = NULL;
	ret = avformat_open_input(&format_context, filename, NULL, NULL);
	if (ret < 0) {
		return 0;
	}

	ret = avformat_find_stream_info(format_context, NULL);
	if (ret < 0) {
		avformat_close_input(&format_context);
		return 0;
	}

	AVStream *audio_stream = NULL;
	for (unsigned int i = 0; i < format_context->nb_streams; i++) {
		if (format_context->streams[i]->codecpar->codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
			audio_stream = format_context->streams[i];
			break;
		}
	}
	if (audio_stream == NULL) {
		avformat_close_input(&format_context);
		return 0;
	}


	codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
	if (!codec) {
		avformat_close_input(&format_context);
		return 0;
	}

	c = avcodec_alloc_context3(codec);
	if (!c) {
		avformat_close_input(&format_context);
		return 0;
	}



	if (avcodec_parameters_to_context(c, audio_stream->codecpar) < 0) {
		avcodec_free_context(&c);
		avformat_close_input(&format_context);
		return 0;
	}


	int nDecodeFS;
	if (nFS != -1) {
		nDecodeFS = nFS;
	}
	else {
		nDecodeFS = c->sample_rate;
	}
	int nDecodeCh;
	if (nCh != -1) {
		nDecodeCh = nCh;
	}
	else {
		nDecodeCh = c->channels;
	}


///////////////////////////////////
	if (audio_stream->codecpar->codec_id == AV_CODEC_ID_ALAC) {
		if (c->sample_fmt == AV_SAMPLE_FMT_S16P 
			|| c->sample_fmt == AV_SAMPLE_FMT_U8P
			|| c->sample_fmt == AV_SAMPLE_FMT_U8
			|| c->sample_fmt == AV_SAMPLE_FMT_S16

			) {
			nBits = 16;
		}
		else {
			nBits = 24;
		}

	}
/////////////////////////////////////////



	if (avcodec_open2(c, codec, NULL) < 0) {
		avcodec_free_context(&c);
		avformat_close_input(&format_context);
		return 0;
	}

///////////////////////////////////////////////////////////////

//resample
	int output_channels = nDecodeCh;
	int output_rate = nDecodeFS;

	int input_channels = c->channels;
	int input_rate = c->sample_rate;
	
	AVSampleFormat input_sample_fmt = c->sample_fmt;
	AVSampleFormat output_sample_fmt = AV_SAMPLE_FMT_FLT;

	SwrContext* resample_ctx = NULL;

	resample_ctx = swr_alloc();
	if (!resample_ctx) {
		avcodec_free_context(&c);
		avformat_close_input(&format_context);
		return 0;
	}

	av_opt_set_int(resample_ctx, "in_channel_layout", av_get_default_channel_layout(input_channels), 0);
	av_opt_set_int(resample_ctx, "in_sample_rate", input_rate, 0);
	av_opt_set_sample_fmt(resample_ctx, "in_sample_fmt", input_sample_fmt, 0);

	av_opt_set_int(resample_ctx, "out_channel_layout", av_get_default_channel_layout(output_channels), 0);
	av_opt_set_int(resample_ctx, "out_sample_rate", output_rate, 0);
	av_opt_set_sample_fmt(resample_ctx, "out_sample_fmt", output_sample_fmt, 0);


	swr_init(resample_ctx);

///////////////////////////////////////////////////////////////


	outfile = fopen(outfilename, "wb+");
	if (!outfile) {
		avcodec_free_context(&c);
		avformat_close_input(&format_context);
		return 0;
	}


	decoded_frame = av_frame_alloc();


	WriteWaveHeader(outfile, 0, nDecodeFS, nDecodeCh, nBits);

	int nConverSamples = 0;
	AVPacket packet = AVPacket();
	while (av_read_frame(format_context, &packet) == 0) {
//		AVPacket *p = &packet;
		if (packet.stream_index == audio_stream->index) {
			if (!decode(c, resample_ctx, output_channels, nBits, output_rate, &packet,decoded_frame, outfile, &nConverSamples)) {
				avcodec_free_context(&c);
				fclose(outfile);
				avcodec_free_context(&c);
				av_frame_unref(decoded_frame);
				av_frame_free(&decoded_frame);
				avformat_close_input(&format_context);
				return 0;
				break;
			}
			if(callbackFunction && pvUserData) {
				if (!callbackFunction((int)(nConverSamples * 100.0 / audio_stream->duration), pvUserData)) {
					swr_free(&resample_ctx);
					av_packet_unref(&packet);
					avcodec_free_context(&c);
					av_frame_unref(decoded_frame);
					av_frame_free(&decoded_frame);
					avformat_close_input(&format_context);
					return 0;


				}
			}
/*
			int *n = (int *)pvUserData;
			if (*n == 1) {
				swr_free(&resample_ctx);
				av_packet_unref(&packet);
				avcodec_free_context(&c);
				av_frame_unref(decoded_frame);
				av_frame_free(&decoded_frame);
				avformat_close_input(&format_context);
				return 0;

			}
*/
/*
			if (avcodec_send_packet(c, &packet) != 0) {
				printf("avcodec_send_packet failed\n");
			}
			while (avcodec_receive_frame(c, decoded_frame) == 0) {
				data_size = av_get_bytes_per_sample(c->sample_fmt);
				if (data_size > 0) {
					for (int i = 0; i < decoded_frame->nb_samples; i++) {
						for (int ch = 0; ch < c->channels; ch++) {
							fwrite(decoded_frame->data[ch] + data_size*i, 1, data_size, outfile);
						}
					}
				}

			}
*/
		}
		av_packet_unref(&packet);
	}
	/* flush the decoder */
	packet.data = NULL;
	packet.size = 0;

	if (!decode(c, resample_ctx, output_channels, nBits, output_rate, &packet, decoded_frame, outfile, &nConverSamples)) {
		fclose(outfile);
		swr_free(&resample_ctx);
		avcodec_free_context(&c);
		av_frame_unref(decoded_frame);
		av_frame_free(&decoded_frame);
		avformat_close_input(&format_context);
		return 0;
	}

	if (!fseek(outfile, 0LL, SEEK_SET)) /* if outf is seekable, rewind and adjust length */
		WriteWaveHeader(outfile, gPcmBytes, nDecodeFS, nDecodeCh, nBits);


	avformat_close_input(&format_context);
	fclose(outfile);

	swr_free(&resample_ctx);
	avcodec_free_context(&c);
	av_frame_unref(decoded_frame);
	av_frame_free(&decoded_frame);

	return 1;
}


int GetAACInfo(TCHAR *szIn, int *nFS, int *nCh, int *Ms, int *bALAC)
{

	const TCHAR *filename;
	const AVCodec *codec;
	AVCodecContext *c = NULL;
	int ret;
	AVFrame *decoded_frame = NULL;

	filename = szIn;

//	av_register_all();
//	avcodec_register_all();
//	avfilter_register_all();

	gPcmBytes = 0;

	atexit(_sswEAExitProc);		//ffmpeg内の関数はエラーが起きるととにかくexit()してしまうので、ここで捕まえる


	AVFormatContext *format_context = NULL;
	ret = avformat_open_input(&format_context, filename, NULL, NULL);
	if (ret < 0) {
		return 0;
	}

	ret = avformat_find_stream_info(format_context, NULL);
	if (ret < 0) {
		avformat_close_input(&format_context);
		return 0;
	}

	AVStream *audio_stream = NULL;
	for (unsigned int i = 0; i < format_context->nb_streams; i++) {
		if (format_context->streams[i]->codecpar->codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
			audio_stream = format_context->streams[i];
			break;
		}
	}
	if (audio_stream == NULL) {
		avformat_close_input(&format_context);
		return 0;
	}



	codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
	if (!codec) {
		avformat_close_input(&format_context);
		return 0;
	}

	*bALAC = audio_stream->codecpar->codec_id == AV_CODEC_ID_ALAC;

	c = avcodec_alloc_context3(codec);
	if (!c) {
		avformat_close_input(&format_context);
		return 0;
	}


	if (avcodec_parameters_to_context(c, audio_stream->codecpar) < 0) {
		avcodec_free_context(&c);
		avformat_close_input(&format_context);
		return 0;
	}


	*nFS = c->sample_rate;
	*nCh = c->channels;
	*Ms = (int)((float)audio_stream->duration / (float)audio_stream->time_base.den * 1000.0);

	avcodec_free_context(&c);
	avformat_close_input(&format_context);
	return 1;
}


ffStreamData *ffStreamDecordInit(TCHAR *szIn, int *nFS, int *nCh)
{

	const TCHAR *filename;
	const AVCodec *codec;
	AVCodecContext *c = NULL;
	int ret;
	AVFrame *decoded_frame = NULL;

	filename = szIn;

	gPcmBytes = 0;

	atexit(_sswEAExitProc);		//ffmpeg内の関数はエラーが起きるととにかくexit()してしまうので、ここで捕まえる


	AVFormatContext *format_context = NULL;
	ret = avformat_open_input(&format_context, filename, NULL, NULL);
	if (ret < 0) {
		return NULL;
	}

	ret = avformat_find_stream_info(format_context, NULL);
	if (ret < 0) {
		return NULL;
	}

	AVStream *audio_stream = NULL;
	for (unsigned int i = 0; i < format_context->nb_streams; i++) {
		if (format_context->streams[i]->codecpar->codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
			audio_stream = format_context->streams[i];
			break;
		}
	}
	if (audio_stream == NULL) {
		return NULL;
	}


	codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
	if (!codec) {
		return NULL;
	}

	c = avcodec_alloc_context3(codec);
	if (!c) {
		return NULL;
	}

	if (avcodec_parameters_to_context(c, audio_stream->codecpar) < 0) {
		avcodec_free_context(&c);
		return NULL;
	}

	if (avcodec_open2(c, codec, NULL) < 0) {
		avcodec_free_context(&c);
		return NULL;
	}

	decoded_frame = av_frame_alloc();

	*nFS = c->sample_rate;
	*nCh = c->channels;

	ffStreamData *ffData = new ffStreamData;
	ffData->audio_stream = audio_stream;
	ffData->avCodecContext = c;
	ffData->decoded_frame = decoded_frame;
	ffData->format_context = format_context;
	ffData->nTotalDecordByte = 0;
	ffData->nDecordByteSize = 0;
	ffData->n1DecordMaxByteSize = 8192 * 2 * c->channels;
	ffData->pDecordData = new char[ffData->n1DecordMaxByteSize * c->channels * 16 / 8];
	return ffData;
}

void ffStreamDecordEnd(ffStreamData *Data)
{
	if (Data) {
		AVCodecContext *c = (AVCodecContext*)Data->avCodecContext;
		AVFrame *decoded_frame = (AVFrame*)Data->decoded_frame;

		delete[] Data->pDecordData;
		avformat_close_input((AVFormatContext **)&Data->format_context);
		avcodec_free_context(&c);
		av_frame_unref(decoded_frame);
		av_frame_free(&decoded_frame);
	}
}


int ffStreamDecord(ffStreamData *Data)
{ 
	if (!Data) return 0;

	AVCodecContext *c = (AVCodecContext*)Data->avCodecContext;
	AVFrame *decoded_frame = (AVFrame*)Data->decoded_frame;
	AVStream *audio_stream = (AVStream*)Data->audio_stream;
	AVFormatContext *format_context = (AVFormatContext*)Data->format_context;

	AVPacket packet = AVPacket();

	Data->nDecordByteSize = 0;

	while (av_read_frame(format_context, &packet) == 0) {
		//		AVPacket *p = &packet;
		if (avcodec_send_packet(c, &packet) != 0) {
		}
		size_t   data_size;
		BOOL bDataExist = FALSE;
		while (avcodec_receive_frame(c, decoded_frame) == 0) {
			data_size = av_get_bytes_per_sample(c->sample_fmt);
			if (data_size > 0) {
				for (int i = 0; i < decoded_frame->nb_samples; i++) {
					for (int ch = 0; ch < c->channels; ch++) {
						int n;
						if (c->sample_fmt == AV_SAMPLE_FMT_FLTP) {
							float* f = (float *)(decoded_frame->data[ch] + data_size * i);
							n = *f * (1 << (16 - 1));
							memcpy(Data->pDecordData + Data->nDecordByteSize, &n, 2);
							Data->nTotalDecordByte += 2;
							Data->nDecordByteSize += 2;

						}
						else if (c->sample_fmt == AV_SAMPLE_FMT_S16P) {
							short* s = (short *)(decoded_frame->data[ch] + data_size * i);
							n = *s;

							memcpy(Data->pDecordData + Data->nDecordByteSize, &n, 2);
							Data->nTotalDecordByte += 2;
							Data->nDecordByteSize += 2;
						}
						else if (c->sample_fmt == AV_SAMPLE_FMT_S32P) {
							long* l = (long *)(decoded_frame->data[ch] + data_size * i);
							n = *l >> 16;
							memcpy(Data->pDecordData + Data->nDecordByteSize, &n, 2);
							Data->nTotalDecordByte += 2;
							Data->nDecordByteSize += 2;
						}
						else if (c->sample_fmt == AV_SAMPLE_FMT_DBLP) {
							double* l = (double *)(decoded_frame->data[ch] + data_size * i);
							n = *l * (1 << (16 - 1));

							memcpy(Data->pDecordData + Data->nDecordByteSize, &n, 2);
							Data->nTotalDecordByte += 2;
							Data->nDecordByteSize += 2;
						}
						else if (c->sample_fmt == AV_SAMPLE_FMT_S64P) {
							_int64* i64 = (_int64 *)(decoded_frame->data[ch] + data_size * i);
							n = *i64 >> 48;
							memcpy(Data->pDecordData + Data->nDecordByteSize, &n, 2);
							Data->nTotalDecordByte += 2;
							Data->nDecordByteSize += 2;
						}
						bDataExist = TRUE;
					}
				}
			}
			if (bDataExist)break;
		}
		av_packet_unref(&packet);
		if (bDataExist)break;
	}
	return Data->nTotalDecordByte;
}


static void EncodeMain(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt,
	AVFormatContext *outctx)
{
	int ret;
/* send the frame for encoding */
	ret = avcodec_send_frame(ctx, frame);
	if (ret < 0) {

	}
//	AVERROR(EAGAIN);   //-11
//	AVERROR(EINVAL);  //-22	
//	AVERROR(ENOMEM);  //-12

	av_init_packet(pkt);
	pkt->data = NULL;
	pkt->size = 0;

	while (ret >= 0) {
		ret = avcodec_receive_packet(ctx, pkt);


		av_packet_rescale_ts(pkt, ctx->time_base, outctx->streams[0]->time_base);

		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {

		}
		else if (ret < 0) {

		}
		else {
			pkt->stream_index = 0;
			av_write_frame(outctx, pkt); 

		}
		av_packet_unref(pkt);
	}
}


int EncodeToAAC
(
	TCHAR *szIn,
	TCHAR *szOut,
	TCHAR *szOutTemp,
	int nCh,			//-1 Same As Wave
	int nFS,			//-1 Same As Wave
	int nBitrate,
	int nKind,  // 0:AAC  1 HEAAV   2 ALAC
	int(CALLBACK*callbackFunction)(int, void*),
	void* pvUserData
)
{
	char *filename;
	AVCodec *codec;
	AVCodecContext *c = NULL;
	AVFrame *frame;
	//	AVPacket *pkt;
	int ret;

	filename = szOut;
	atexit(_sswEAExitProc);		//ffmpeg内の関数はエラーが起きるととにかくexit()してしまうので、ここで捕まえる
/*
	AVFormatContext *format_context = NULL;
	ret = avformat_open_input(&format_context, szIn, NULL, NULL);
	if (ret < 0) {
		fclose(f);
		return 0;
	}

	ret = avformat_find_stream_info(format_context, NULL);
	if (ret < 0) {
		fclose(f);
		return 0;
	}

	AVStream *audio_stream = NULL;
	for (unsigned int i = 0; i < format_context->nb_streams; i++) {
		if (format_context->streams[i]->codecpar->codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
			audio_stream = format_context->streams[i];
			break;
		}
	}
	if (audio_stream == NULL) {
		fclose(f);
		return 0;
	}
*/


	FILE *fIn = fopen(szIn, "rb");
	if (!fIn) {
		return 0;
	}
	int WaveFS, WaveCh, WaveBits, nWaveDataByte;
	ReadPCMWaveHeader(fIn, &WaveFS, &WaveCh, &WaveBits, &nWaveDataByte);

	if (nCh == -1) nCh = WaveCh;
	if (nFS == -1) nFS = WaveFS;

/////////////////////////////////////////

	if (nKind == 0 || nKind == 1) {
		codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	}
	else {
		codec = avcodec_find_encoder(AV_CODEC_ID_ALAC);
	}
	if (!codec) {
		return 0;
	}

	c = avcodec_alloc_context3(codec);
	if (!c) {
		return 0;
	}
	c->bit_rate = nBitrate;

	if (nKind == 1) {
		c->profile = FF_PROFILE_AAC_HE;
	}
	else if (nKind == 0) {
		c->profile = FF_PROFILE_AAC_LOW;
	}

	if (nKind == 2) {
		c->sample_rate = WaveFS;
	}
	else {
		const int *p = codec->supported_samplerates;
		if (!p) c->sample_rate = 44100;
		else {
			int sample_rate = 0;
			while (*p) {
				if (nFS == *p) {
					sample_rate = nFS;
					break;
				}
				if (*p > sample_rate) {
					sample_rate = *p;
				}
				p++;
			}
			c->sample_rate = sample_rate;
		}
	}
	const uint64_t *p1;
	p1 = codec->channel_layouts;

	if (nCh == 2) {
		c->channel_layout = AV_CH_LAYOUT_STEREO;
	}
	else {
		c->channel_layout = AV_CH_LAYOUT_MONO;
	}
/*	if (p1) {
		while (*p1) {
			int nb_channels = av_get_channel_layout_nb_channels(*p1);
			if (nb_channels == nCh) {
				c->channel_layout = *p1;
				break;
			}
			p1++;
		}
	}
*/
	c->channels = av_get_channel_layout_nb_channels(c->channel_layout);


	const enum AVSampleFormat *format = codec->sample_fmts;
	if (!format) {
		avcodec_free_context(&c);
		return 0;

	}
	if (nKind == 2) {
		if (WaveBits == 16 || WaveBits == 8) {
			c->sample_fmt = AV_SAMPLE_FMT_S16P;
		}
		else {
			c->sample_fmt = AV_SAMPLE_FMT_S32P;
//			c->bits_per_raw_sample = 24;
		}
	}
	else {
		c->sample_fmt = *format;
	}
/*	while (*format != AV_SAMPLE_FMT_NONE) {
		if (*format == (AVSampleFormat)audio_stream->codecpar->format) {
			c->sample_fmt = *format;
			break;
		}
		format++;
	}
*/
/////////////////////////////////////////////////////////////////
	if (avcodec_open2(c, codec, NULL) < 0) {
		avcodec_free_context(&c);
		return 0;
	}
//////////////////////////////////////////////////////////////////

	FILE *fOut = fopen(szOutTemp, "wb");
	if (!fOut) {
		avcodec_free_context(&c);
		fclose(fIn);
		return 0;
	}


	int output_channels = c->channels;
	int output_rate = c->sample_rate;

	int input_channels = WaveCh;
	int input_rate = WaveFS;

	AVSampleFormat input_sample_fmt = AV_SAMPLE_FMT_FLTP;
	AVSampleFormat output_sample_fmt = AV_SAMPLE_FMT_FLT;

	SwrContext* resample_ctx = NULL;
	resample_ctx = swr_alloc();
	if (!resample_ctx) {
		avcodec_free_context(&c);
		fclose(fIn);
		return 0;
	}

	av_opt_set_int(resample_ctx, "in_channel_layout", av_get_default_channel_layout(input_channels), 0);
	av_opt_set_int(resample_ctx, "in_sample_rate", input_rate, 0);
	av_opt_set_sample_fmt(resample_ctx, "in_sample_fmt", input_sample_fmt, 0);

	av_opt_set_int(resample_ctx, "out_channel_layout", av_get_default_channel_layout(output_channels), 0);
	av_opt_set_int(resample_ctx, "out_sample_rate", output_rate, 0);
	av_opt_set_sample_fmt(resample_ctx, "out_sample_fmt", output_sample_fmt, 0);
	swr_init(resample_ctx);

	int nSampleByte = av_get_bytes_per_sample(output_sample_fmt);

	int WaveSamples = 1024;
	char *pWaveData[2];

	int nDstTotalSample = 0;

	for (int i = 0; i < 2; i++) {
		pWaveData[i] = new char[WaveSamples * sizeof(float)];
	}
	int nTotalReadByte = 0;
	int out_samples;
	uint8_t **out_buffer;
	int dst_nb_samples;
	int dst_linesize;
	while(1) {
		if (nTotalReadByte + WaveSamples * WaveCh * WaveBits / 8 > nWaveDataByte) {
			WaveSamples = (nWaveDataByte - nTotalReadByte) / (WaveCh * WaveBits / 8);
		}
		WaveSamples = GetWaveBuffer(fIn, pWaveData, WaveSamples, WaveCh, WaveBits);
		nTotalReadByte += WaveSamples * WaveCh * WaveBits / 8;


		dst_nb_samples = av_rescale_rnd(swr_get_delay(resample_ctx, input_rate) +
			WaveSamples, output_rate, input_rate, AV_ROUND_UP);

		ret = av_samples_alloc_array_and_samples(&out_buffer, &dst_linesize, output_channels,
			dst_nb_samples, output_sample_fmt, 0);

		out_samples = swr_convert(resample_ctx, out_buffer, dst_nb_samples,
			(const uint8_t **)pWaveData, WaveSamples);

		if (!out_samples) break;

		if (callbackFunction && pvUserData) {
			if (!callbackFunction((int)(nTotalReadByte * 100.0 / nWaveDataByte / 2), pvUserData)) {
				fclose(fOut);
				fclose(fIn);
				swr_free(&resample_ctx);
				avcodec_free_context(&c);
				for (int i = 0; i < 2; i++) {
					delete[] pWaveData[i];
				}
				return 0;


			}
		}
/*
		int *n = (int *)pvUserData;
		if (*n == 1) {
			fclose(fOut);
			fclose(fIn);
			swr_free(&resample_ctx);
			avcodec_free_context(&c);
			for (int i = 0; i < 2; i++) {
				delete[] pWaveData[i];
			}
			return 0;

		}
*/

		nDstTotalSample += out_samples;
		fwrite(out_buffer[0], 1, out_samples * nSampleByte * output_channels, fOut);
		av_freep(&out_buffer);

	}
	for (int i = 0; i < 2; i++) {
		delete [] pWaveData[i];
	}

	fclose(fOut);
	fclose(fIn);
	swr_free(&resample_ctx);

///////////////////////////////////////////////////////////////////////////

	fIn = fopen(szOutTemp, "rb");
	if (!fIn) {
		av_frame_free(&frame);
		avcodec_free_context(&c);
		fclose(fIn);
		return 0;
	}

	AVIOContext* io_context = nullptr;
	if (avio_open(&io_context, filename, AVIO_FLAG_WRITE) < 0) {
		avcodec_free_context(&c);
		return 0;
	}
	AVFormatContext *outctx;
	if (avformat_alloc_output_context2(&outctx, NULL, NULL, filename) < 0) {
		avcodec_free_context(&c);
		avio_close(io_context);
		return 0;

	}
	outctx->pb = io_context;
	outctx->audio_codec = codec;
	outctx->audio_codec_id = c->codec_id;

	// generate global header when the format requires it
	if (outctx->oformat->flags & AVFMT_GLOBALHEADER) {
		outctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}



	AVStream *audio_st = avformat_new_stream(outctx, codec);
	audio_st->sample_aspect_ratio = c->sample_aspect_ratio;
	audio_st->time_base = c->time_base;
	avcodec_parameters_from_context(audio_st->codecpar, c);

	ret = avformat_write_header(outctx, NULL);

///////////////////////////////////////////////////////////
	frame = av_frame_alloc();
	if (!frame) {
		avcodec_free_context(&c);
		avio_closep(&io_context);
		avio_close(io_context);
		return 0;
	}

	frame->nb_samples = c->frame_size;
	frame->format = c->sample_fmt;
	frame->channel_layout = c->channel_layout;

	ret = av_frame_get_buffer(frame, 0);
	if (ret < 0) {
		avcodec_free_context(&c);
		av_frame_free(&frame);
		avio_closep(&io_context);
		avio_close(io_context);
		return 0;
	}


	int nConvertSamples = 0;
	AVPacket packet = AVPacket();


	int aud_frame_counter = 0;
	uint8_t *pFrameBuffer[2];
	int nReadTotalSample = 0;
	int nReadSample = c->frame_size;

	while (nReadTotalSample < nDstTotalSample) {
		ret = av_frame_make_writable(frame);

		pFrameBuffer[0] = (uint8_t*)frame->data[0];
		pFrameBuffer[1] = (uint8_t*)frame->data[1];
	
		if (nReadTotalSample + nReadSample > nDstTotalSample) {
			nReadSample = nDstTotalSample - nReadTotalSample;
		}
		nReadTotalSample += nReadSample;
		if (nKind == 2) {
			if (WaveBits == 16 || WaveBits == 8) {
				memset(pFrameBuffer[0], 0, sizeof(int16_t) * nReadSample);
			}
			else {
				memset(pFrameBuffer[0], 0, sizeof(int32_t) * nReadSample);
			}
		}
		else {
			memset(pFrameBuffer[0], 0, nSampleByte * nReadSample);
		}

		if (c->channels == 2) {
			if (nKind == 2) {
				if (WaveBits == 16 || WaveBits == 8) {
					memset(pFrameBuffer[1], 0, sizeof(int16_t) * nReadSample);
				}
				else {
					memset(pFrameBuffer[1], 0, sizeof(int32_t) * nReadSample);
				}
			}
			else {
				memset(pFrameBuffer[1], 0, nSampleByte * nReadSample);
			}
		}
		for (int i = 0; i < nReadSample; i++) {
			int data_size = fread(pFrameBuffer[0], 1, sizeof(float), fIn);
			if (nKind == 2) {
				float f = *(float *)pFrameBuffer[0];
				if (WaveBits == 16 || WaveBits == 8) {
					int16_t s16 = (int16_t)(f * (1 << 15));
					if (s16 > 32767) {
						s16 = 32767;
					}
					if (s16 < -32768) {
						s16 = -32768;
					}
					*(int16_t *)pFrameBuffer[0] = s16;
					pFrameBuffer[0] += sizeof(int16_t);
				}
				else {
					double d = f;
					int64_t s64 = (int64_t)(d * (1 << (32 - 1)));
					if (s64 >INT_MAX) {
						s64 = INT_MAX;
					}
					if (s64 < INT_MIN) {
						s64 = INT_MIN;
					}

					int32_t s32 = s64;

					*(int32_t *)pFrameBuffer[0] = s32;
					pFrameBuffer[0] += sizeof(int32_t);
				}
			}
			else {
				pFrameBuffer[0] += sizeof(float);
			}

			if (c->channels == 2) {
				data_size = fread(pFrameBuffer[1], 1, sizeof(float), fIn);
				if (nKind == 2) {
					float f = *(float *)pFrameBuffer[1];
					if (WaveBits == 16 || WaveBits == 8) {
						int16_t s16 = (int16_t)(f * (1 << 15));
						if (s16 > 32767) {
							s16 = 32767;
						}
						if (s16 < -32768) {
							s16 = -32768;
						}
						*(int16_t *)pFrameBuffer[1] = s16;
						pFrameBuffer[1] += sizeof(int16_t);
					}
					else {
						double d = f;
						int64_t s64 = (int64_t)(d * (1 << (32 - 1)));
						if (s64 >INT_MAX) {
							s64 = INT_MAX;
						}
						if (s64 < INT_MIN) {
							s64 = INT_MIN;
						}
						int32_t s32 = s64;

						*(int32_t *)pFrameBuffer[1] = s32;
						pFrameBuffer[1] += sizeof(int32_t);
					}

				}
				else {
					pFrameBuffer[1] += sizeof(float);
				}
			}
		}

		frame->nb_samples = nReadSample;
		frame->pts = aud_frame_counter++ * c->frame_size;

		EncodeMain(c, frame, &packet, outctx);

		if (callbackFunction && pvUserData) {
			if (!callbackFunction((int)(50.0 + nReadTotalSample * 100.0 / nDstTotalSample / 2), pvUserData)) {
				av_write_trailer(outctx);
				avio_closep(&io_context);
				avio_close(io_context);
				avcodec_free_context(&c);
				av_frame_free(&frame);
				return 0;
			}
		}
/*
		int *n = (int *)pvUserData;
		if (*n == 1) {
			av_write_trailer(outctx);
			avio_closep(&io_context);
			avio_close(io_context);
			avcodec_free_context(&c);
			av_frame_free(&frame);
			return 0;

		}
*/


	}
	EncodeMain(c, NULL, &packet, outctx);
	av_write_trailer(outctx);

	avio_closep(&io_context);
	avio_close(io_context);
	avcodec_free_context(&c);
	av_frame_free(&frame);
	return 1;

}