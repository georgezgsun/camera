// dllmain.cpp : Defines the entry point for the DLL application.
//#include "pch.h"
#include <time.h>
#include <ctime>
#include <iostream>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
//#include <Windows.h>
#include <pthread.h>
using namespace std;
#include "ffmpeg.h"

/*BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}*/

//namespace ffmpeg
//{
// take a picture from the packet's key frame
// @param pkt		The candidate packet, it is not referenced
// @param filename	the file name that the picture is going to be saved to
// @return			0 for success, non 0 error code for failaure
int take_picture(AVPacket* pkt, std::string filename)
{
    int err = -1;
    if (pkt && pkt->flags | AV_PKT_FLAG_KEY && !filename.empty())
    {
        FILE* jpeg_file = fopen(filename.c_str(), "wb");
        if (!jpeg_file)
        {
            fwrite(pkt->data, 1, pkt->size, jpeg_file);
            fclose(jpeg_file);
        }
    }

    return err;
}

AudioEncoder::AudioEncoder()
{
    m_codec_ctx = NULL;
    m_codec = NULL;
    m_message = "";
    m_err = 0;
}

AudioEncoder::~AudioEncoder()
{
    // clean up the codec context
    if (m_codec_ctx)
    {
        avcodec_free_context(&m_codec_ctx);
    }
}

// receive a packet from the encoder
// @param pkt	the packet been encoded
// @return		number of packet received, 0 for not available or end of file, 1 for success, negative for error code
int AudioEncoder::receive_packet(AVPacket* pkt)
{
    m_err = avcodec_receive_packet(m_codec_ctx, pkt);
    if (m_err < 0)
    {
        m_message = "error while encoding";
        return m_err;
    }

    // continue until the output is available
    if (m_err == AVERROR(EAGAIN))
    {
        m_message = "packet not available";
        return 0;
    }

    // check if the decoder has been flushed
    if (m_err == AVERROR_EOF)
    {
        m_message = "encoder get fully flushed";
        return 0;
    }

    return 1;
}

// send a frame to encoder
// @param frame	the frame to be encoded
// @return		0 for success, negative for error code
int AudioEncoder::send_frame(AVFrame* frame)
{
    m_err = avcodec_send_frame(m_codec_ctx, frame);
    if (m_err < 0)
    {
        m_message = "error when sending a frame to encoder";
    }
    return m_err;
}


// get the error message of last operation
string AudioEncoder::get_error_message()
{
    return m_message;
}

// assign the codec context from stream's codec parameters
int AudioEncoder::open(AVStream* stream)
{
    m_codec = avcodec_find_encoder(stream->codecpar->codec_id);
    if (!m_codec)
    {
        m_err = -1;
        m_message = "cannot find encoder for " + std::to_string(stream->codecpar->codec_id);
        return m_err;
    }

    m_codec_ctx = avcodec_alloc_context3(m_codec);
    if (!m_codec_ctx)
    {
        m_err = -2;
        m_message = "cannot allocate an encoding context for " + std::to_string(stream->codecpar->codec_id);
        return m_err;
    }

    // assign the codec context with all gotten parameters from the stream
    avcodec_parameters_to_context(m_codec_ctx, stream->codecpar);
    m_codec_ctx->time_base = stream->time_base;

    // assign those critical parameters that not specified in the stream
    m_err = -1;
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        // check if the sample format matches
        const enum AVSampleFormat* fmt = m_codec->sample_fmts;
        while (*fmt != AV_SAMPLE_FMT_NONE)
        {
            if (*fmt == m_codec_ctx->sample_fmt)
            {
                m_err = 0;
                break;
            }
            fmt++;
        }

        if (m_err < 0)
        {
            m_message = "Sample format mismatch";
            return m_err;
        }

        // Select the highest supported samplerate
        m_codec_ctx->sample_rate = 44100;
        if (m_codec->supported_samplerates)
        {
            m_codec_ctx->sample_rate = m_codec->supported_samplerates[0];
            for (int i = 0; m_codec->supported_samplerates[i]; i++)
            {
                if (abs(44100 - m_codec->supported_samplerates[i]) < abs(44100 - m_codec_ctx->sample_rate))
                {
                    m_codec_ctx->sample_rate = m_codec->supported_samplerates[i];
                }
            }
        }

        // Select stereo channel layout
        m_codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
        m_codec_ctx->channels = 2;

        m_err = avcodec_open2(m_codec_ctx, m_codec, NULL);
        m_message = "codec opened";
        if (m_err < 0)
        {
            m_message = "cannot open the codec";
        }
        return m_err;
    }

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        for (int i = 0; ; i++)
        {
            if (m_codec->pix_fmts[i] < 0)
            {
                m_err = -1;
                m_message = "pixel format in the stream is invalid to the encoder";
                return m_err;
            }

            if (m_codec->pix_fmts[i] == stream->codecpar->format)
            {
                m_codec_ctx->pix_fmt = m_codec->pix_fmts[i];
                break;
            }
        }
    }


    //m_codec_ctx->codec_type = stream->codecpar->codec_type;
    //m_codec_ctx->codec_id = stream->codecpar->codec_id;
    //m_codec_ctx->codec_tag = stream->codecpar->codec_tag;

    //m_codec_ctx->bit_rate = stream->codecpar->bit_rate;
    //m_codec_ctx->bits_per_coded_sample = stream->codecpar->bits_per_coded_sample;
    //m_codec_ctx->bits_per_raw_sample = stream->codecpar->bits_per_raw_sample;
    //m_codec_ctx->profile = stream->codecpar->profile;
    //m_codec_ctx->level = stream->codecpar->level;

    //// assign the local codec parameters with the stream codec parameters
    //if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    //{
    //	//m_codec_ctx->sample_fmt = m_codec->sample_fmts ? m_codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    //	m_codec_ctx->sample_fmt = map_audio[stream->codecpar->format + 1];
    //	m_codec_ctx->bit_rate = 64000;
    //	m_codec_ctx->sample_rate = 44100;
    //	if (m_codec->supported_samplerates)
    //	{
    //		m_codec_ctx->sample_rate = m_codec->supported_samplerates[0];
    //		for (int i = 0; m_codec->supported_samplerates[i]; i++)
    //		{
    //			if (m_codec->supported_samplerates[i] == 44100)
    //			{
    //				m_codec_ctx->sample_rate = 44100;
    //			}
    //		}
    //	}
    //	m_codec_ctx->channels = 2;
    //	m_codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    //	m_codec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    //}

    //if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    //{
    //	m_codec_ctx->codec_id = stream->codecpar->codec_id;
    //	m_codec_ctx->bit_rate = 4000000;
    //	m_codec_ctx->width = stream->codecpar->width;
    //	m_codec_ctx->height = stream->codecpar->height;
    //	m_codec_ctx->field_order = stream->codecpar->field_order;
    //	m_codec_ctx->color_range = stream->codecpar->color_range;
    //	m_codec_ctx->color_primaries = stream->codecpar->color_primaries;
    //	m_codec_ctx->color_trc = stream->codecpar->color_trc;
    //	m_codec_ctx->colorspace = stream->codecpar->color_space;
    //	m_codec_ctx->chroma_sample_location = stream->codecpar->chroma_location;
    //	m_codec_ctx->sample_aspect_ratio = stream->codecpar->sample_aspect_ratio;
    //	m_codec_ctx->has_b_frames = stream->codecpar->video_delay;

    //	//m_codec_ctx->gop_size = stream->codec->gop_size;
    //	//m_codec_ctx->pix_fmt = stream->codec->pix_fmt;
    //	//m_codec_ctx->pix_fmt = stream->codecpar->format;
    //	m_codec_ctx->time_base = stream->time_base;
    //}

    return m_err;
}

AudioFifo::AudioFifo()
{
    m_channels = 1;
    m_sample_fmt = AV_SAMPLE_FMT_NONE;
    m_fifo = NULL;
    m_err = 0;
    m_message = "";
}

AudioFifo::~AudioFifo()
{
    av_audio_fifo_free(m_fifo);
}

AudioFifo::AudioFifo(enum AVSampleFormat sample_fmt, int channels)
{
    m_sample_fmt = sample_fmt;
    m_channels = channels;
    m_fifo = av_audio_fifo_alloc(m_sample_fmt, m_channels, 1);

    if (m_fifo)
    {
        m_err = 0;
        m_message = "";
    }
    else
    {
        m_err = -1;
        m_message = "cannot allocate the audio fifo";
    }
}

// Add audio samples to the fifo buffer
// @param input_samples	Samples to be added. The dimensions are channel, sample
// @param frame_size	Number of samples to be added
// @return				error code (0 for success)
int AudioFifo::add_samples(uint8_t** input_samples, int nb_samples)
{
    // make the fifo as lardge as it needs to be to hold both the original and new samples
    m_err = av_audio_fifo_realloc(m_fifo, av_audio_fifo_size(m_fifo) + nb_samples);
    if (m_err < 0)
    {
        m_message = "cannot add memory to fifo";
        return m_err;
    }

    // store the new samples in the fifo buffer
    if (av_audio_fifo_write(m_fifo, (void**)input_samples, nb_samples) < nb_samples)
    {
        m_message = "cannot write data to fifo";
        m_err = -1;
    }

    return m_err;
}

// Pop audio samples from the fifo buffer
// @param (out)	output_samples	Place where samples to be loaded. The dimensions are channel, sample
// @param		frame_size		Number of samples to be loaded
// @return						Number of samples actually loaded
int AudioFifo::pop_samples(uint8_t** output_samples, int nb_samples)
{
    return av_audio_fifo_read(m_fifo, (void**)output_samples, nb_samples);
}

// Removes data from the fifo without reading it
// @param	nb_samples	number of samples to drain
// @return	0 for success, negtive for AVERROR code on failure
int AudioFifo::drain_samples(int nb_samples)
{
    m_message = "";
    m_err = av_audio_fifo_drain(m_fifo, nb_samples);
    if (m_err < 0)
    {
        m_message = "Error while drain the fifo";
    }
    return m_err;
}

// Get the current number of samples in the fifo available for reading
// @return	number of samples available for reading
int AudioFifo::get_size()
{
    return av_audio_fifo_size(m_fifo);
}

char* av_err(int ret)
{
    // buffer to store error messages
    static char buf[256];
    if (ret < 0)
        av_strerror(ret, buf, sizeof(buf));
    else
        memset(buf, 0, sizeof(buf));

    return buf;
}

const std::string get_date_time()
{
    time_t t = std::time(0); // get current time
    char buf[50];
    tm *now = localtime(&t);
    strftime(buf, sizeof(buf), "%F-%H%M%S", now);

    return buf;
}

AudioDecoder::AudioDecoder()
{
    m_err = 0;
    m_message = "";
    m_codec_ctx = NULL;
    m_codec = NULL;
}

int AudioDecoder::open(AVStream* stream)
{
    // check if the stream is empty
    if (!stream)
    {
        m_err = -1;
        m_message = "Empty stream";
        return m_err;
    }

    // check if it is an audio stream
    if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
    {
        m_err = -2;
        m_message = "not a valid audio stream";
        return m_err;
    }

    // try to find the decoder
    m_codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!m_codec)
    {
        m_err = -3;
        m_message = "codec " + std::to_string(stream->codecpar->codec_id) + " not found";
        return m_err;
    }

    m_codec_ctx = avcodec_alloc_context3(m_codec);
    if (!m_codec_ctx)
    {
        m_err = -4;
        m_message = "could not allocate audio codec contexst";
        return m_err;
    }

    // open the decoder
    if (avcodec_open2(m_codec_ctx, m_codec, NULL) < 0)
    {
        m_err = -5;
        m_message = "could not open audio codec";
        return m_err;
    }

    m_err = 0;
    return m_err;
}

int AudioDecoder::send_packet(AVPacket* pkt)
{
    m_err = avcodec_send_packet(m_codec_ctx, pkt);
    if (m_err < 0)
    {
        m_message = "error when sending packet to decoder";
    }
    return m_err;
}

// try to receive the decoded frame
// return 0 when there is no frame available yet ot the decoder has been flushed
// return negative when there is an error
// return 1 when get a frame, you may try again for the next available frame
int AudioDecoder::receive_frame(AVFrame* frame)
{
    m_err = avcodec_receive_frame(m_codec_ctx, frame);
    if (m_err < 0)
    {
        m_message = "error while decoding";
        return m_err;
    }

    // continue until the output is available
    if (m_err == AVERROR(EAGAIN))
    {
        m_message = "frame not available";
        return 0;
    }

    // check if the decoder has been flushed
    if (m_err == AVERROR_EOF)
    {
        m_message = "decoder get fully flushed";
        return 0;
    }

    return 1;
}

// get the error message of last operation
std::string AudioDecoder::get_error_message()
{
    return m_message;
}

HWDecoder::HWDecoder()
{
    m_decoder_Ctx = NULL;
    m_decoder = NULL;
    m_hw_device_Ctx = NULL;
    m_hw_pix_fmt = AV_PIX_FMT_NONE;

    m_err = 0;
    m_message = "";
}

HWDecoder::~HWDecoder()
{
    avcodec_free_context(&m_decoder_Ctx);
    av_buffer_unref(&m_hw_device_Ctx);
}

// open the decoder by type and the codec parameter specified in the stream
int HWDecoder::open(AVStream* stream, std::string device)
{
    enum AVHWDeviceType type = av_hwdevice_find_type_by_name(device.c_str());
    if (type == AV_HWDEVICE_TYPE_NONE)
    {
        m_err = -1;
        m_message = "Device " + device + " unsupported";
        return m_err;
    }

    m_err = av_hwdevice_ctx_create(&m_hw_device_Ctx, type, "auto", NULL, 0);
    if (m_err < 0)
    {
        m_message = "Cannot open the hardware device";
        return m_err;
    }

    if (stream->codecpar->codec_id == AV_CODEC_ID_H264)
    {
        m_decoder = avcodec_find_decoder_by_name("h264_qsv");
    }
    else
    {
        m_decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    }

    int i;
    for (i = 0;; i++)
    {
        const AVCodecHWConfig* config = avcodec_get_hw_config(m_decoder, i);
        if (!config)
        {
            m_err = -2;
            m_message = "Decoder ";
            m_message.append(m_decoder->name);
            m_message.append(" does not support device type ");
            m_message.append(av_hwdevice_get_type_name(type));
            return m_err;
        }

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type)
        {
            m_hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    m_decoder_Ctx = avcodec_alloc_context3(m_decoder);
    if (!m_decoder_Ctx)
    {
        m_err = -1;
        m_message = " cannot allocate memory for ";
        m_message.append(m_decoder->long_name);
        return m_err;
    }

    m_err = avcodec_parameters_to_context(m_decoder_Ctx, stream->codecpar);
    if (m_err < 0)
    {
        m_message = "cannot assign decoder parameters";
        return m_err;
    }

    m_decoder_Ctx->get_format = get_hw_format;

    m_err = avcodec_open2(m_decoder_Ctx, m_decoder, NULL);
    if (m_err < 0)
    {
        m_message = "failed to open codec";
    }

    return m_err;
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts)
{
    const enum AVPixelFormat* p;
    for (p = pix_fmts; *p != -1; p++)
    {
        if (*p == m_hw_pix_fmt)
            return *p;
    }

    //HWDecoder::m_message = "failed to get HW surface format";
    return AV_PIX_FMT_NONE;
}

// send a packet to the decoder
// an empty packet means to flush
// return 0 on success
// negative return means something
int HWDecoder::send_packet(AVPacket* pkt)
{
    m_err = avcodec_send_packet(m_decoder_Ctx, pkt);
    if (m_err < 0)
    {
        m_message = "error when sending packet to decoder";
    }
    return m_err;
}

// try to receive the decoded frame
// return 0 when there is no frame available yet ot the decoder has been flushed
// return negative when there is an error
// return 1 when get a frame, you may try again for the next available frame
int HWDecoder::receive_frame(AVFrame* frame)
{
    AVFrame* hw_frame = NULL;
    uint8_t* buffer = NULL;

    if (!(hw_frame = av_frame_alloc()) || !(frame = av_frame_alloc()))
    {
        m_err = -1;
        m_message = "cannot allocate frame";
    }

    while (1)
    {
        m_err = avcodec_receive_frame(m_decoder_Ctx, hw_frame);
        if (m_err < 0)
        {
            m_message = "error while decoding";
            return m_err;
        }

        // continue until the output is available
        if (m_err == AVERROR(EAGAIN))
        {
            continue;
        }

        // check if the decoder has been flushed
        if (m_err == AVERROR_EOF)
        {
            av_frame_free(&hw_frame);
            av_frame_free(&frame);

            m_err = 0;
            m_message = "decoder get fully flushed";
            return m_err;
        }

        if (hw_frame->format == m_hw_pix_fmt)
        {
            // retrieve data from GPU to CPU
            m_err = av_hwframe_transfer_data(frame, hw_frame, 0);
            av_frame_free(&hw_frame);
            if (m_err < 0)
            {
                m_message = "error transferring the data from GPU to CPU";
                av_frame_free(&frame);
            }
            else
            {
                m_err = 1;
            }
            return m_err;
        }
    }

    return m_err;
}

// get the error message of last operation
std::string HWDecoder::get_error_message()
{
    return m_message;
}

Demuxer::Demuxer()
{
    m_url = "";
    m_ifmt_Ctx = NULL;
    m_options = NULL;
    m_index_video = -1;
    m_index_audio = -1;

    m_message = "";
    m_err = avformat_network_init();
    avdevice_register_all();
    m_ifmt_Ctx = avformat_alloc_context();
    m_format = "rtsp";
    m_start_time = 0;
    m_wclk_align = true;
}

Demuxer::~Demuxer()
{
    avformat_free_context(m_ifmt_Ctx);
}

// get the error message of last operation
std::string Demuxer::get_error_message()
{
    return m_message;
}

AVFormatContext* Demuxer::get_input_format_context()
{
    m_err = 0;
    m_message = "";

    return m_ifmt_Ctx;
}

// get the stream time base of the source
AVRational Demuxer::get_stream_time_base(int stream_index)
{
    m_err = 0;
    m_message = "";

    if (stream_index >= 0 && static_cast <unsigned int>(stream_index) < m_ifmt_Ctx->nb_streams)
    {
        return m_ifmt_Ctx->streams[stream_index]->time_base;
    }

    m_err = -1;
    m_message = "Error. No input format context is found.";
    return AVRational{ 1,1 };
};

// Set the options used to open a camera
// additional options are
//  -format value, specify the camera format. dshow for a Webcam in windows, v4l2 for a Webcam in linux
//  -wall_clock value, wall clock alignment. true to get pts in epoch, false to get original pts
int Demuxer::set_options(std::string option, std::string value)
{
    m_err = 0;
    m_message = "";

    if (option == "format")
    {
        m_format = value;
        m_message = "update the camera format to be " + value;
    }
    else if (option == "wall_clock")
    {
        if (value == "false")
        {
            m_wclk_align = false;

            m_err = 0;
            m_message = "wall clock alignment is off";
        }
        else if (value == "true")
        {
            m_wclk_align = true;

            m_err = 0;
            m_message = "wall clock alignment is on";
        }
        else
        {
            m_message = "unkown value of '" + value + "' for 'wall clock alignment' setting";
            m_err = -1;
        }
        return m_err;
    }
    else
    {
        m_err = av_dict_set(&m_options, option.c_str(), value.c_str(), 0);
        m_message = "set the option '" + option + "' to be " + value;
    }
    return m_err;
}

// open/connect to the camera
// return 0 on success
int Demuxer::open(std::string url)
{
    // check if the url is empty
    if (url.empty())
    {
        m_err = -1;
        m_message = "Error. Camera path is empty";
        return m_err;
    }
    m_url = url;

    // to determine the camera type
    m_message = "IP Camera: ";
    AVInputFormat* ifmt = NULL;
    if (url.find("/dev/") != std::string::npos)
    {
        ifmt = av_find_input_format("v4l2");
        m_message = "v4l2: ";
    }
    else
    {
        ifmt = av_find_input_format(m_format.c_str());
        m_message = m_format + ": "; // "USB (Windows)";
    }

    m_err = avformat_open_input(&m_ifmt_Ctx, m_url.c_str(), ifmt, &m_options);
    if (m_err < 0)
    {
        m_message.append(av_err(m_err));
        return m_err;
    }
    m_start_time = av_gettime();  // hold global start time in microseconds

    m_err = avformat_find_stream_info(m_ifmt_Ctx, 0);
    if (m_err < 0)
    {
        m_message.append(av_err(m_err));
        return m_err;
    }

    m_err = 0;
    m_message += " connected";

    // find the index of video stream
    AVStream* st;
    for (unsigned int i = 0; i < m_ifmt_Ctx->nb_streams; i++)
    {
        st = m_ifmt_Ctx->streams[i];

        // modify the start time when required aligning to wall clock
        if (m_wclk_align)
        {
            // calculate the correct time stamp for a very large epoch time
            int64_t den = st->time_base.den;
            int64_t num = st->time_base.num;
            num *= 1000000;

            int64_t gcd = av_gcd(den, num);
            if (gcd)
            {
                den /= gcd;
                num /= gcd;
            }

            if (num > den)
            {
                st->start_time = m_start_time * den / num - st->start_time;
            }
            else
            {
                st->start_time = m_start_time / num * den - st->start_time;
            }
        }

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            m_index_video = i;
            st->duration = st->time_base.den / st->time_base.num / 30; // default duration for video is 1/30s
            m_message += ", video stream found";
        }

        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            m_index_audio = i;
            st->duration = st->time_base.den / st->time_base.num / 100; // default duration for audio is 1/100s
            m_message += ", audio stream found";
        }
        st->duration++; // make sure it is not 0
    }

    if (m_index_video < 0 && m_index_audio < 0)
    {
        m_err = -2;
        m_message += ", but no video nor audio stream";
    }
    if (m_index_video != -1)
    {

      m_pCodecCtx = m_ifmt_Ctx->streams[m_index_video]->codec;
      AVCodec* pCodec;
      pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
      m_err = avcodec_open2(m_pCodecCtx, pCodec, NULL);
      if(m_err <0 )
         m_message += ", can not open video codec";
    }
    if (m_index_audio != -1)
    {
        m_aCodecCtx = m_ifmt_Ctx->streams[m_index_audio]->codec;
        AVCodec* aCodec = NULL;
        aCodec = avcodec_find_decoder(m_aCodecCtx->codec_id);
        m_err = avcodec_open2(m_aCodecCtx, aCodec, NULL);
        if(m_err < 0)
        m_message += ", can not open audio codec";
    }

    return m_err;
}

// read a packet from the camera
// return 0 on success
int Demuxer::read_packet(AVPacket* pkt)
{
   // m_message = "";
    m_err = av_read_frame(m_ifmt_Ctx, pkt); // read a frame from the camera

    // handle the timeout
    if (m_err < 0)
    {
        m_message.assign(av_err(m_err));
        m_message = "Time out while reading " + m_url + " with error " + m_message;
        pkt = NULL;
        return m_err;
    }

    if (!m_wclk_align)
    {
        return m_err;
    }

    // Doing wall clock alignment
    if (pkt->pts == AV_NOPTS_VALUE)
    {
        m_err = -1;
        m_message = "no wall clock alignment for packet without pts value";
        return m_err;
    }

    // make pts and pds wall clock aligned
    AVStream* st = m_ifmt_Ctx->streams[pkt->stream_index];
    pkt->pts += st->start_time;
    pkt->dts += st->start_time;
    if (!pkt->duration)
    {
        pkt->duration = st->duration; // assign duration to be the default duration
    }

    return m_err;
}

// get the video stream index of the camera
// negative return indicates no video stream in the camera
int Demuxer::get_video_index()
{
    return m_index_video;
}

// get the audio stream index of the camera
// negative return indicates no audio stream in the camera
int Demuxer::get_audio_index()
{
    return m_index_audio;
}

// get the specified stream
// index can be video index or audio index
AVStream* Demuxer::get_stream(int stream_index)
{
    if (stream_index < 0 || static_cast <unsigned int>(stream_index) >= m_ifmt_Ctx->nb_streams)
    {
        m_err = -1;
        m_message = "In valid stream index specified";
        return NULL;
    }

    m_err = 0;
    m_message = "get the stream";
    return m_ifmt_Ctx->streams[stream_index];
}

CircularBuffer::CircularBuffer()
{
    first_pkt = NULL;
    last_pkt = NULL;
    bg_pkt = NULL;
    mn_pkt = NULL;

    m_total_packets = 0;
    m_size = 0;
    m_time_span = 0;
    m_MaxSize = 0;
    m_codecpar = avcodec_parameters_alloc(); //must be allocated with avcodec_parameters_alloc() and freed with avcodec_parameters_free().
    m_st = (AVStream*)av_mallocz(sizeof(AVStream));
    m_pts_span = 0;
    m_stream_index = 0;
    m_time_base = AVRational{ 1, 2 };

    m_err = 0;
    m_message = "";
    m_last_pts = 0;

    flag_writing = false;
    flag_reading = false;

}

void CircularBuffer::open(int time_span, int max_size)
{
    first_pkt = NULL;
    last_pkt = NULL;
    bg_pkt = NULL;
    mn_pkt = NULL;

    //
    m_total_packets = 0;
    m_size = 0;
    m_pts_span = 0;
    m_time_span = time_span > 0 ? time_span : 0;
    m_MaxSize = max_size > 0 ? max_size : 0;
    m_stream_index = 0;
    m_time_base = AVRational{ 1, 2 };

    m_err = 0;
    m_message = "";

    flag_writing = false;
    flag_reading = false;
}

CircularBuffer::~CircularBuffer()
{
    while (first_pkt)
    {
        AVPacketList* pktl = first_pkt;
        av_packet_unref(&pktl->pkt);
        first_pkt = pktl->next;
        av_free(pktl);
    }

    avcodec_parameters_free(&m_codecpar);
}

// set the stream info
// stream id is to indentify the stream index when push packet
// stream time base is used to calculate the time span
// stream codec parameters are also saved for furture usage
int CircularBuffer::add_stream(AVStream* stream)
{
    // check the stream
    if (!stream)
    {
        m_err = -1;
        m_message = "Empty stream is not allowed.";
        return m_err;
    }

    // copy the codec parameters to local
    m_err = avcodec_parameters_copy(m_codecpar, stream->codecpar);
    if (m_err < 0)
    {
        m_message.assign(av_err(m_err));
        return m_err;
    }

    // copy a couple of important parameters to local stream
    m_st->codecpar = m_codecpar; // store the same codec parameters in the local stream
    m_st->time_base = stream->time_base;
    m_st->start_time = stream->start_time;
    m_st->r_frame_rate = stream->r_frame_rate;
    m_st->avg_frame_rate = stream->avg_frame_rate;
    m_st->sample_aspect_ratio = stream->sample_aspect_ratio;

    m_time_base = stream->time_base;
    m_stream_index = stream->index;
    m_pts_span = m_time_span * m_time_base.den / m_time_base.num;

    // clear the circular buffer in case the stream is changed
    while (first_pkt)
    {
        AVPacketList* pktl = first_pkt;
        av_packet_unref(&pktl->pkt);
        first_pkt = pktl->next;
        av_free(pktl);
    }

    m_err = 0;
    m_message = "";
    return m_err;
}

// push a video or audio packet to the circular buffer
// 0 or positive return indicates the packet is added successfully. The number returned is the number of packets disposed from the circular buffer.
// negative return indicates no packet is added due to an error.
int CircularBuffer::push_packet(AVPacket* pkt)
{
    // empty packet is not allowed in the circular buffer
    if (!pkt)
    {
        m_err = -1;
        m_message = "packet unacceptable: empty";
        return -1; // return number directly for multithread safe pupose, m_err is not safe
    }

    // packet with different stream index is not accepted
    if (pkt->stream_index != m_stream_index)
    {
        m_err = -2;
        m_message = "packet unacceptable: stream index is different";
        return -2; // return number directly for multithread safe pupose, m_err is not safe
    }

    // packet that cannot be reference is not allowed
    if (av_packet_make_refcounted(pkt) < 0)
    {
        m_err = -3;
        m_message = "packet unacceptable: cannot be referenced";
        return -3;  // return number directly for multithread safe pupose, m_err is not safe
    }

    // packet that has no pts is not allowed
    if (pkt->pts == AV_NOPTS_VALUE)
    {
        m_err = -4;
        m_message = "packet unacceptable: has no valid pts";
        return -4;  // return number directly for multithread safe pupose, m_err is not safe
    }

    // new a packet list, which is going to be freed when getting staled later
    AVPacketList* pktl = (AVPacketList*)av_mallocz(sizeof(AVPacketList));
    if (!pktl)
    {
        //av_packet_unref(pkt);
        av_free(pktl);
        m_err = -5;
        m_message = "cannot allocate new packet list";
        return m_err;
    }

    // packet that is non monotonically increasing
    if (m_last_pts == 0)
    {
        m_last_pts = pkt->pts;
    }

    if (pkt->pts < m_last_pts)
    {
        m_err = -6;
        m_message = "packet unacceptable: non monotonically increasing";
        return -6; // return number directly for multithread safe pupose, m_err is not safe
    }

    // add the packet to the queue
    //av_packet_move_ref(&pktl->pkt, pkt);  // this makes the pkt unref
    av_packet_ref(&pktl->pkt, pkt);  // leave the pkt alone
    pktl->next = NULL;

    // set the writing flag to block unsafe reading in other process
    flag_writing = true;

    // modify the pointers
    if (!last_pkt)
        first_pkt = pktl; // handle the first adding
    else
        last_pkt->next = pktl;
    last_pkt = pktl; // the new added packet is always the last packet in the circular buffer

    m_total_packets++;
    m_size += pktl->pkt.size + sizeof(*pktl);

    // make sure it is multithread safe before modifing any critical variables
    if (flag_reading)
    {
        //m_message = "Warning: not modify the pointer for others are reading";  // it is unsafe to modify m_message now
        flag_writing = false; // clear the writing flag at the very end
        return 0; // return number directly for multithread safe pupose, m_err is not safe
    }

    // update the background reader when a new packet is added
    if (!bg_pkt)
    {
        bg_pkt = last_pkt;
    }

    // update the main reader when a new packet is added
    if (!mn_pkt)
    {
        mn_pkt = last_pkt;
    }

    // maintain the circular buffer by kicking out those overflowed packets
    int64_t pts_bg = bg_pkt->pkt.pts;
    int64_t pts_mn = mn_pkt->pkt.pts;
    int64_t allowed_pts = last_pkt->pkt.pts - m_pts_span;
    m_err = m_total_packets; // used to store original number of packets
    while ((first_pkt->pkt.pts < allowed_pts) || (m_size > m_MaxSize))
    {
        pktl = first_pkt;
        m_total_packets--; // update the number of total packets
        m_size -= first_pkt->pkt.size + sizeof(*first_pkt);  // update the size of the circular buffer

        av_packet_unref(&first_pkt->pkt); // unref the first packet
        first_pkt = first_pkt->next; // update the first packet list
        av_freep(&pktl);  // free the unsed packet list
    }

    // update the background reader in case it is behind the first packet
    if (pts_bg < first_pkt->pkt.pts)
    {
        bg_pkt = first_pkt;
    }

    // update the mn reader in case it is behind the first packet changed
    if (pts_mn < first_pkt->pkt.pts)
    {
        mn_pkt = first_pkt;
    }

    m_err -= m_total_packets; // calculate how many packets are disposed
    m_message = "Packet added";
    flag_writing = false; // clear the writting flag at the very end of pushing
    return m_err;
}

// read a packet out of the circular buffer.
// read a packet using the background reader when isBackground is true
// read a packet using the main reader when isBackground is false
// return (0 or 1) indicates the number of packet is read.
int CircularBuffer::peek_packet(AVPacket* pkt, bool isBackground)
{
    // no reading while others are reading or writing to keep multithread operations safe
    if (flag_writing || flag_reading)
    {
        pkt = NULL;
        return 0;
    }

    flag_reading = true; // set the reading flag to stop the modify of packet list

    if (isBackground && bg_pkt)
    {
        av_packet_ref(pkt, &bg_pkt->pkt); // expose to the outside a copy of the packet
        bg_pkt = bg_pkt->next; // update the reader packet
        flag_reading = false;
        m_err = 1;
        m_message = "packet read for background recording";
        return m_err; // return the number of packets read
    }

    if (!isBackground && mn_pkt)
    {
        av_packet_ref(pkt, &mn_pkt->pkt); // expose to the outside a copy of the packet
        mn_pkt = mn_pkt->next; // update the reader packet
        flag_reading = false;
        m_err = 1;
        m_message = "packet read for main recording";
        return m_err;
    }

    pkt = NULL;
    m_err = 0;
    m_message = "no packet available to read at this moment";
    flag_reading = false;
    return 0;
}

// get the time base of the circular buffer
AVRational CircularBuffer::get_time_base()
{
    m_err = 0;
    m_message = "";

    return m_time_base;
}

// get the size of the circular buffer
int CircularBuffer::get_size()
{
    m_err = 0;
    m_message = "";

    return m_size;
}

// get the number of packets in the circular buffer
int CircularBuffer::get_total_packets()
{
    return m_total_packets;
}

// get the codec parameters of the circular buffer
AVCodecParameters* CircularBuffer::get_stream_codecpar()
{
    m_err = 0;
    m_message = "";

    return m_codecpar;
}

// get the stream assigned to the circular buffer
AVStream* CircularBuffer::get_stream()
{
    return m_st;
}

// get the error message of last operation
std::string CircularBuffer::get_error_message()
{
    return m_message;
}

// reset the main reader to the very beginning of the circular buffer
void CircularBuffer::reset_main_reader()
{
    m_err = 0;
    m_message = "";

    mn_pkt = first_pkt;
}

Muxer::Muxer()
{
    m_url = "";
    m_ofmt_Ctx = NULL;
    m_options = NULL;
    m_index_video = -1;
    m_index_audio = -1;
    m_flag_interleaved = true;
    m_flag_wclk = true;
    m_tbf_video = AVRational{ 1,2 };
    m_tbf_audio = AVRational{ 1,3 };
    m_time_base_audio = AVRational{ 1,4 };
    m_time_base_video = AVRational{ 1,5 };
    m_pts_offset_video = 0;
    m_pts_offset_audio = 0;
    m_defalt_duration_audio = 0;
    m_defalt_duration_video = 0;
    m_err = 0;
    m_message = "";
    m_chunk_time = 0; // chunk indicator
    m_chunk_interval = 0;
    m_chunk_prefix = "";
    m_format = "mp4";
}

Muxer::~Muxer()
{
    avformat_free_context(m_ofmt_Ctx);
    av_dict_free(&m_options);
}

int Muxer::set_options(std::string option, std::string value)
{
    m_err = 0;
    m_message = "";

    if (option == "wall_clock")
    {
        if (value == "false")
        {
            m_flag_wclk = false;

            m_err = 0;
            m_message = "'wall clock alignment' flag is set to false";
        }
        else if (value == "true")
        {
            m_flag_wclk = true;

            m_err = 0;
            m_message = "'wall clock alignment' flag is set to true";
        }
        else
        {
            m_message = "unkown value of '" + value + "' for 'wall clock alignment' flag setting.";
            m_err = -1;
        }
        return m_err;
    }

    if (option == "interleaved_write")
    {
        if (value == "false")
        {
            m_flag_interleaved = false;

            m_err = 0;
            m_message = "'interleaved writting' flag is set to false";
        }
        else if (value == "true")
        {
            m_flag_interleaved = true;

            m_err = 0;
            m_message = "'interleaved writting' flag is set to true";
        }
        else
        {
            m_message = "unkown value of '" + value + "' for 'interleaved writting' flag setting.";
            m_err = -1;
        }
        return m_err;
    }

    if (option == "format")
    {
        if (value.length() < 1 || value.length() > 10)
        {
            m_format = "mp4";

            m_err = -1;
            m_message = value + " is invalid for 'file format' option setting";
        }
        else
        {
            m_format = value;
            m_message = "'file format' option is set to be " + value;
            m_err = 0;
        }

        return m_err;
    }
    return av_dict_set(&m_options, option.c_str(), value.c_str(), 0);
}

// add a stream to the muxer
// @param stream	stream to be added. The time base, codec parameters are used
// @return			stream id in muxer. 0 or positive on success, negative for error code
int Muxer::add_stream(AVStream* stream)
{
    m_err = 0;
    m_message = "";

    // return an error code when no stream can be found
    if (!stream)
    {
        m_err = -1;
        m_message = "Error. Empty stream cannot be added";
        return m_err;
    }

    // Create a new format context for the output container format if it is empty
    if (!m_ofmt_Ctx)
    {
        //m_err = avformat_alloc_output_context2(&m_ofmt_Ctx, NULL, "mp4", NULL);
        m_err = avformat_alloc_output_context2(&m_ofmt_Ctx, NULL, m_format.c_str(), NULL);
        if (m_err < 0)
        {
            //m_message = "Error. Could not allocate output format context.";
            m_message.assign(av_err(m_err));
            return m_err;
        }
    }

    //m_ifmt_Ctx = ifmt_Ctx;
    AVStream* out_stream = avformat_new_stream(m_ofmt_Ctx, NULL);
    if (!out_stream)
    {
        m_err = -2;
        m_message = "Error. Failed allocating output stream.";
        return m_err;
    }

    m_err = avcodec_parameters_copy(out_stream->codecpar, stream->codecpar);
    if (m_err < 0)
    {
        m_err = -3;
        //m_message = "Error. Failed to copy the stream codec parameters.";
        m_message.assign(av_err(m_err));
        return m_err;
    }

    out_stream->id = m_ofmt_Ctx->nb_streams - 1;
    out_stream->codecpar->codec_tag = 0;

    if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        m_index_video = out_stream->id;
        m_time_base_video = stream->time_base;
        m_video_stream = out_stream;
    }
    else if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        m_index_audio = out_stream->id;
        m_time_base_audio = stream->time_base;
        m_audio_stream = out_stream;
    }

    m_message = "stream is added to the recorder";
    return out_stream->id;
}

// add a stream from the circular buffer
// @param circular_buffer	the circular buffer whose embedded stream will be added.
// @return					stream id in muxer. 0 or positive on success, negative for error code
int Muxer::add_stream(CircularBuffer* circular_buffer)
{
    return add_stream(circular_buffer->get_stream());
}

// add streams from the input format context
// @param fmt_ctx	the input format context whose embedded streams will be added.
// @return			stream id in muxer. 0 or positive on success, negative for error code
int Muxer::add_stream(AVFormatContext* fmt_ctx)
{
    unsigned int i;
    for (i = 0; i < fmt_ctx->nb_streams; i++)
    {
        m_err = add_stream(fmt_ctx->streams[i]);
        if (m_err < 0)
        {
            m_message = "Not able to add stream [" + std::to_string(i) + "]";
            return m_err;
        }
    }

    return m_err;
}

// open a new muxer
// @param url				can be a filename, rtsp, rtp, or other valid url
// @param chunk_interval	the muxer (video file) will be automatically chunked every chunk_interval seconds
// @return					0 on success, negative for error code
int Muxer::open(std::string url, int chunk_interval)
{
    if (url.empty())
    {
        m_err = -1;
        m_message = "url cannot be empty";
        return m_err;
    }

    // validate the input chunk interval
    if (chunk_interval < 0 || chunk_interval > 3600)
    {
        m_err = -1;
        m_message = "'chunk interval' setting shall be [0-3600]";
        return m_err;
    }

    // positive chunk interval indicates segmentally recording, url is considered to be the file prefix of segmental files
    if (chunk_interval > 0)
    {
        m_chunk_prefix = url;
        m_chunk_interval = chunk_interval * 1000;  // convert the interval into miliseconds
        m_message = "chunked recording is set to be " + m_chunk_prefix + "yyyy-MM-dd-hhmmss" + m_chunk_prefix;
    }
    else
    {
        m_url = url;  // url is the full path of the recording video file
        m_chunk_interval = 0;
        m_chunk_prefix = "";
        m_message = "normal recording";
    }

    m_chunk_time = 0;
    return chunk();
}

// make another chunked recording
// first stop current recording in case there is one. Then start another recording by chunk prefix.
// @return 0 on success, negative for error code
int Muxer::chunk()
{
    // to check the chunk setting
    if (m_chunk_prefix.empty() || !m_chunk_interval)
    {
        m_err = -1;
        m_message = "Invalid chunk settings.";
        return m_err;
    }

    // uses m_chunk_time as an indicateor of first recording
    if (m_chunk_time)
    {
        m_err = close();

        if (m_err)
        {
            return m_err;
        }
    }

    // set next chunk time, at x:xx:00 if wall clock alignment is set
    m_chunk_time = m_flag_wclk ?
        (av_gettime() / 1000 / m_chunk_interval + 1) * m_chunk_interval
        : ((av_gettime() + 500000) / 1000000) * 1000 + m_chunk_interval; // align to 1s

    // file name is set as <prefix><yyyy-MM-dd-hhmmss>.<ext>
    m_url = m_chunk_prefix+ get_date_time() + "." + m_format;

    // try to solve the
    AVStream* st;
    for (int i = 0; static_cast <unsigned int>(i) < m_ofmt_Ctx->nb_streams; i++)
    {
        st = m_ofmt_Ctx->streams[i];
        //m_ofmt_Ctx->streams[i]->start_time = AV_NOPTS_VALUE;
        //m_ofmt_Ctx->streams[i]->duration = AV_NOPTS_VALUE;
        //m_ofmt_Ctx->streams[i]->first_dts = AV_NOPTS_VALUE;
        //m_ofmt_Ctx->streams[i]->cur_dts = AV_NOPTS_VALUE;
        st->start_time = AV_NOPTS_VALUE;
        st->duration = static_cast<int64_t>(m_chunk_interval) * 90;
        st->first_dts = AV_NOPTS_VALUE;
        st->cur_dts = 0;
    }
    m_ofmt_Ctx->output_ts_offset = 0;

    if (!(m_ofmt_Ctx->oformat->flags & AVFMT_NOFILE))
    {
        m_err = avio_open(&m_ofmt_Ctx->pb, m_url.c_str(), AVIO_FLAG_WRITE);
        if (m_err < 0)
        {
            m_message.assign(av_err(m_err));
            m_message = "Could not open " + m_url + " with error " + m_message;
            return m_err;
        }
    }
    else
    {
        m_err = -1;
        m_message = "Error: Previous recording is not closed.";
        return m_err;
    }

    //m_err = avformat_write_header(m_ofmt_Ctx, &dictionary);
    m_err = avformat_write_header(m_ofmt_Ctx, &m_options);
    if (m_err < 0)
    {
        m_message.assign(av_err(m_err));
        m_message += " Could not open " + m_url;
        return m_err;
    }

    m_message = m_url + " is openned with return code " + std::to_string(m_err);
    m_err = 0;

    // update the time base factors used to rescale the time stamps of input packets to the output stream
    m_tbf_audio = m_time_base_audio;
    m_tbf_video = m_time_base_video;
    m_defalt_duration_audio = 440;
    m_defalt_duration_video = 3000;

    if (m_index_audio >= 0)
    {
        m_tbf_audio.den *= m_ofmt_Ctx->streams[m_index_audio]->time_base.num;
        m_tbf_audio.num *= m_ofmt_Ctx->streams[m_index_audio]->time_base.den;
        m_defalt_duration_audio = m_ofmt_Ctx->streams[m_index_audio]->time_base.den / m_ofmt_Ctx->streams[m_index_audio]->time_base.num / 44000;
    }

    if (m_index_video >= 0)
    {
        m_tbf_video.den *= m_ofmt_Ctx->streams[m_index_video]->time_base.num;
        m_tbf_video.num *= m_ofmt_Ctx->streams[m_index_video]->time_base.den;
        m_defalt_duration_video = m_ofmt_Ctx->streams[m_index_video]->time_base.den / m_ofmt_Ctx->streams[m_index_video]->time_base.num / 30;
    }
    m_pts_offset_audio = 0;
    m_pts_offset_video = 0;

    return m_err;
}

// write one frame/packet in the muxer
// @param pkt			the packet that going to be written
// @param stream_index	the tream index of the muxer
// @return				0 on success, negative for error, 1 for chunked
int Muxer::record(AVPacket* pkt, int stream_index)
{
    m_err = 0;
    m_message = "";

    if (pkt->pts == AV_NOPTS_VALUE)
    {
        m_err = -1;
        m_message = "packet unacceptable: has no valid pts";

        av_packet_unref(pkt);
        return m_err;
    }

    if (pkt->dts == AV_NOPTS_VALUE)
    {
        pkt->dts = pkt->pts;
    }

    if (pkt->size == 0)
    {
        m_err = -2;
        m_message = "packet unacceptable: empty";

        av_packet_unref(pkt);
        return m_err;
    }

    // rescale the time stamp to the output stream
    if (stream_index == m_index_audio)
    {
        pkt->pts *= m_tbf_audio.num / m_tbf_audio.den;
        pkt->dts *= m_tbf_audio.num / m_tbf_audio.den;

        if (pkt->duration)
        {
            pkt->duration *= m_tbf_audio.num / m_tbf_audio.den;
        }
        else
        {
            pkt->duration = m_defalt_duration_audio;
        }

        if (m_pts_offset_audio == 0)
        {
            m_pts_offset_audio = -pkt->pts;
        }
        pkt->pts += m_pts_offset_audio;
        pkt->dts += m_pts_offset_audio;
    }
    else if (stream_index == m_index_video)
    {
        pkt->pts *= m_tbf_video.num / m_tbf_video.den;
        pkt->dts *= m_tbf_video.num / m_tbf_video.den;
        if (pkt->duration)
        {
            pkt->duration *= m_tbf_video.num / m_tbf_video.den;
        }
        else
        {
            pkt->duration = m_defalt_duration_video;
        }

        if (m_pts_offset_video == 0)
        {
            m_pts_offset_video = -pkt->pts;
        }
        pkt->pts += m_pts_offset_video;
        pkt->dts += m_pts_offset_video;
    }

    pkt->stream_index = stream_index;
    pkt->pos = -1;

    // check the interleaved flag
    if (m_flag_interleaved)
    {
        m_err = av_interleaved_write_frame(m_ofmt_Ctx, pkt); // interleaved write will handle the packet unref
    }
    else
    {
        m_err = av_write_frame(m_ofmt_Ctx, pkt);
    }
    av_packet_unref(pkt);

    if (m_err)
    {
        m_message.assign(av_err(m_err));
        return m_err;
    }
    m_message = "packet written";

    m_err = 0;
    int64_t t = av_gettime() / 1000;
    if (m_chunk_time && t >= m_chunk_time)
    {
        m_err = chunk();
        if (!m_err)
        {
            m_err = 1; // indicates get chunked
        }
    }
    //m_err = m_chunk_time && av_gettime() / 1000 >= m_chunk_time ? re_open() : 0;

    return m_err;
}

int Muxer::close()
{
    // no close when no file is opened
    if (m_ofmt_Ctx->oformat->flags & AVFMT_NOFILE)
        return -1;

    m_err = av_write_trailer(m_ofmt_Ctx);
    if (m_err < 0)
    {
        m_message.assign(av_err(m_err));
        return m_err;
    }

    avio_closep(&m_ofmt_Ctx->pb);

    m_message = m_url + " is closed.";
    return m_err;
}

// get the stream codec parameter
AVFormatContext* Muxer::get_output_format_context()
{
    m_err = 0;
    m_message = "";

    return m_ofmt_Ctx;
}

// get the stream time base
AVRational Muxer::get_stream_time_base(int stream_index)
{
    m_err = 0;
    m_message = "";

    if (stream_index >= 0 && static_cast <unsigned int>(stream_index) < m_ofmt_Ctx->nb_streams)
    {
        return m_ofmt_Ctx->streams[stream_index]->time_base;
    }

    return AVRational{ 1,1 };
}

// get the error message of last operation
std::string Muxer::get_error_message()
{
    return m_message;
}

// get the recording filename or url
std::string Muxer::get_url()
{
    return m_url;
}

//}

