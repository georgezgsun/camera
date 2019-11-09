
#include <stdio.h>
#include <string>
#include <iostream>
#include <string>
#include <string.h>

#define ALIGN_TO_WALL_CLOCK 1

// A demo instance of Camera module using circular buffer
// 1. Test the circular buffer
// 2. Test the saving of background recording video files together with main event recordings from single IP camera using circular buffer and two threads structure.
// 3. A sub thread reads the stream from specified camera and saves packet into circular buffer
// 4. The main thread reads packets from circular buffer via two seperqated pointers. One is for background recording. The other one is for main event recording
// 5. Test chunks of recordings

//namespace ffmpeg
//{
    extern "C"
    {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>

#include <libavutil/avutil.h>
#include <libavutil/timestamp.h>
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>

#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
//#include <SDL.h>
//#include <SDL_thread.h>

    }

    char* av_err(int ret);
    const std::string get_date_time();

    static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts);
    static enum AVPixelFormat m_hw_pix_fmt;

    class CircularBuffer
    {
    public:
        CircularBuffer();
        ~CircularBuffer();

        // Add the stream into the circular buffer
        //int add_stream(AVFormatContext* ifmt_Ctx, int stream_index = 0);
        void open(int time_span, int max_size);

        // set the stream info
        // stream id is to indentify the stream index when push packet
        // stream time base is used to calculate the time span
        // stream codec parameters are also saved for furture usage
        int add_stream(AVStream* stream);

        // push a video or audio packet to the circular buffer
        // 0 or positive return indicates the packet is added successfully. The number returned is the number of packets disposed from the circular buffer.
        // negative return indicates no packet is added due to an error.
        int push_packet(AVPacket* pkt);

        // read a packet out of the circular buffer.
        // read a packet using the background reader when isBackground is true
        // read a packet using the main reader when isBackground is false
        // return (0 or 1) indicates the number of packet is read.
        int peek_packet(AVPacket* pkt, bool isBackground = true);

        // reset the main reader to very beginning
        void reset_main_reader();

        // get the stream codec parameters that defines the packet in the circular buffer
        AVCodecParameters* get_stream_codecpar();

        // get the stream associated to the circular buffer
        AVStream* get_stream();

        // get the stream time base
        AVRational get_time_base();

        // get the circular buffer size
        int get_size();

        // get the number of packets in the circular buffer
        int get_total_packets();

        // get the error message of last operation
        std::string get_error_message();

    protected:
        AVPacketList* first_pkt; // pointer to the first added packet in the circular buffer
        AVPacketList* last_pkt; // pointer to the new added packet in the circular buffer
        AVPacketList* bg_pkt; // the background reading pointer
        AVPacketList* mn_pkt; // the main reading pointer
        AVCodecParameters* m_codecpar; // The codec parameters of the bind stream
        AVStream* m_st; // The assigned stream

        int m_total_packets; // counter of total packets in the circular buffer
        int m_size;  // total size of the packets in the buffer
        int m_time_span;  // max time span in seconds
        int64_t m_pts_span; // pts span
        int64_t m_last_pts;  // last valid pts
        AVRational m_time_base; // the time base of the bind stream
        int m_stream_index; // the desired stream index
        int m_MaxSize; // the maximum size allowed for the circular buffer

        int m_err; // the error code of last operation
        std::string m_message; // the error message of last operation

        bool flag_writing; // flag indicates adding new packet to the circular buffer
        bool flag_reading; // flag indicates reading from the circular buffer
    };

    class Muxer
    {
    public:
        Muxer();
        ~Muxer();

        // add a stream to the muxer
        int add_stream(AVStream* stream);

        // add a stream from the circular buffer
        int add_stream(CircularBuffer* circular_buffer);

        // add streams from the input format context
        int add_stream(AVFormatContext* fmt_ctx);

        // open a video recorder specified by url, can be a filename or a rtp url
        // chunk > 0 indicates save to a series of chunked file in interval chunk seconds, url is used as prefix
        int open(std::string url, int chunk = 0);

        // make another chunked recording
        // first stop current recording in case there is one. Then start another recording by chunk prefix.
        // return 0 on success
        int chunk();

        // close the video recorder
        int close();

        // save the packet to the video recorder
        // the stream index specify the audio or video
        int record(AVPacket* pkt, int stream_index = 0);

        // set the options for video recorder, has to be called before open
        int set_options(std::string option, std::string value);

        // get the output format context
        AVFormatContext* get_output_format_context();

        // get the stream time base
        AVRational get_stream_time_base(int stream_index = 0);

        // get the error message of last operation
        std::string get_error_message();

        // get the recording filename or url
        std::string get_url();

    protected:
        std::string m_url;
        AVFormatContext* m_ofmt_Ctx;
        AVDictionary* m_options;
        AVStream* m_video_stream;
        AVStream* m_audio_stream;
        int m_index_video;
        int m_index_audio;
        int m_chunk_interval;
        int64_t m_chunk_time;
        bool m_flag_interleaved;
        bool m_flag_wclk;

        AVRational m_tbf_video; // the factor to rescale the packet time stamp to fit output video stream
        AVRational m_tbf_audio; // the factor to rescale the packet time stamp to fit output audio stream
        AVRational m_time_base_audio;  // the time base of input audio stream
        AVRational m_time_base_video;  // the time base of input video stream

        int64_t m_pts_offset_video;
        int64_t m_pts_offset_audio;
        int64_t m_defalt_duration_audio;
        int64_t m_defalt_duration_video;

        int m_err; // the error code of last operation
        std::string m_message; // the error message of last operation
        std::string m_chunk_prefix;
        std::string m_format;
    };

    class Demuxer
    {
    public:
        Demuxer();
        ~Demuxer();

        // set the options for video recorder, has to be called before open
        int set_options(std::string option, std::string value);

        // open the camera for reading
        int open(std::string url = "");

        // read a packet from the camera
        int read_packet(AVPacket* pkt);

        // get the input format context
        AVFormatContext* get_input_format_context();

        // get the stream time base
        AVRational get_stream_time_base(int stream_index = 0);

        // get the specified stream
        // index can be video index or audio index
        AVStream* get_stream(int stream_index = 0);

        // get the video stream index of the camera
        // negative return indicates no video stream in the camera
        int get_video_index();

        // get the audio stream index of the camera
        // negative return indicates no audio stream in the camera
        int get_audio_index();

        // get the error message of last operation
        std::string get_error_message();

    protected:
        AVCodecContext* m_pCodecCtx;
        AVCodecContext* m_aCodecCtx;
        std::string m_url;
        AVFormatContext* m_ifmt_Ctx;
        AVDictionary* m_options;
        int64_t m_start_time;  // hold the start time when the camera was opened
        int m_index_video;
        int m_index_audio;
        bool m_wclk_align;

        int m_err; // the error code of last operation
        std::string m_message; // the error message of last operation
        std::string m_format; // the camera format, can be rtsp, rtp, v4l2, dshow, file
    };

    class HWDecoder
    {
    public:
        HWDecoder();
        ~HWDecoder();

        // open the decoder by type and the codec parameter specified in the stream
        int open(AVStream* stream, std::string device = "h264-qsv");

        // send a packet to the decoder
        int send_packet(AVPacket* pkt);

        // get the decoded frame
        int receive_frame(AVFrame* frame);

        // get the error message of last operation
        std::string get_error_message();

    protected:

        AVCodecContext* m_decoder_Ctx;
        AVCodec* m_decoder;
        AVBufferRef* m_hw_device_Ctx;

        std::string m_message; // the error message of last operation
        int m_err; // the error code of last operation
    };

    class AudioDecoder
    {
    public:
        AudioDecoder();
        ~AudioDecoder();

        // open the decoder specified in the stream
        int open(AVStream* stream);

        // send a packet to the decoder
        int send_packet(AVPacket* pkt);

        // get the decoded frame
        int receive_frame(AVFrame* frame);

        // get the error message of last operation
        std::string get_error_message();

    protected:
        AVCodecContext* m_codec_ctx;
        AVCodec* m_codec;
        std::string m_message;
        int m_err;
    };

    class AudioFifo
    {
    public:
        AudioFifo();
        AudioFifo(enum AVSampleFormat sample_fmt, int channels);
        ~AudioFifo();

        int add_samples(uint8_t** input_samples, int nb_samples);
        int pop_samples(uint8_t** output_samples, int nb_samples);
        int drain_samples(int nb_samples);
        int get_size();

    protected:
        AVAudioFifo* m_fifo;
        enum AVSampleFormat m_sample_fmt;
        int m_channels;
        int m_err;
        std::string m_message;
    };

    class AudioEncoder
    {
    public:
        AudioEncoder();
        ~AudioEncoder();

        int open(AVStream* stream);

        // send a packet to the decoder
        int receive_packet(AVPacket* pkt);

        // get the decoded frame
        int send_frame(AVFrame* frame);

        // get the error message of last operation
        std::string get_error_message();

    protected:
        AVCodecContext* m_codec_ctx;
        AVCodec* m_codec;
        std::string m_message;
        int m_err;

    };

    class AV_proc
    {
    public:
        //AV_proc();
        //~AV_proc();

        //int open(AVStream* istream);



    };

//}



