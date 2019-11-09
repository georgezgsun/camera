#include <iostream>
#include <stdio.h>
#include "ffmpeg.h"

using namespace std;

static Demuxer *ipCam;
static CircularBuffer* cbuf;

static std::string prefix_videofile ="/mnt/ssd/videos/";
static int Debug = 0;

void* videoCapture(void* argument)
{
    AVPacket pkt;
    int ret, i;
    //i = *((int*)argument);
    AVRational tb = ipCam->get_stream_time_base();
    tb.num *= 1000; // change the time base to be ms based
    int index_video = ipCam->get_video_index();

    // read packets from IP camera and save it into circular buffer
    while (true)
    {
        //ret = av_read_frame(ifmt_Ctx, &pkt); // read a frame from the camera
        ret = ipCam->read_packet(&pkt);

        // handle the timeout, blue screen shall be added here
        if (ret < 0)
        {
            fprintf(stderr, "%s, error code=%d.\n", ipCam->get_error_message().c_str(), ret);
            continue;
        }

        if (pkt.stream_index == index_video)
        {
            ret = cbuf->push_packet(&pkt);  // add the packet to the circular buffer
            if (ret >= 0)
            {
                if (Debug)
                {
                    fprintf(stderr, "Added a new packet (%ldms, %d). Poped %d packets. The circular buffer has %d packets with size %d now.\n",
                        pkt.pts * tb.num / tb.den, pkt.size, ret, cbuf->get_total_packets(), cbuf->get_size());
                }
            }
            else
            {
                fprintf(stderr, "Error %s. Packet (%lldms, %d) is not added. The circular buffer has %d packets with size %d now\n",
                    cbuf->get_error_message().c_str(), pkt.pts * tb.num / tb.den, pkt.size, cbuf->get_total_packets(), cbuf->get_size());
            }
        }
        av_packet_unref(&pkt); // handle the release of the packet here
    }
}


int main(int argc, char * argv[])
{
    // The IP camera
    string CameraPathes[] {
        "rtsp://10.0.9.111/user=admin_password=tlJwpbo6_channel=1_stream=0.sdp",
        "rtsp://10.0.9.112/h264",
        "rtsp://10.0.9.113:8554/0",
        "rtsp://10.0.9.114/type=0&id=1"
        "rtsp://10.0.9.116/1/h264major",
        "rtsp://10.0.9.117:8554/0",
        "rtsp://10.0.9.118/wdx/media/stream.h264",
        "rtsp://10.0.9.119/1/h264major",
        "rtsp://192.168.1.27:554/user=admin_password=tlJwpbo6_channel=1_stream=0.sdp",
        "rtsp://192.168.1.186:554/user=admin_password=tlJwpbo6_channel=1_stream=0.sdp"
        };
    string CameraPath { CameraPathes[0] };
    string CameraName { "Cam0" };
    AVStream* input_stream;

    if (argc > 1)
    {
        int i = atoi(argv[1]);
        if (i > 10)
        {
            i = 9;
        }
        CameraPath = CameraPathes[i];
        CameraName = "Cam" + to_string(i);
    };

    fprintf(stderr, "Now starting the test %s on %s.\n", CameraName.c_str(), CameraPath.c_str());
    prefix_videofile.append(CameraName + "-"); // add the camera name to video file prefix

    ipCam = new Demuxer();

    // ip camera options
    //ipCam->set_options("buffer_size", "200000");
    //ipCam->set_options("rtsp_transport", "tcp");
    ipCam->set_options("stimeout", "100000");
    /*CameraPath = "video=USB Camera";
    CameraPath = "video=Logitech Webcam C930e";*/
    // USB camera options
    /*ipCam->set_options("video_pin_name", "0");
    ipCam->set_options("video_size", "1280x720");
    ipCam->set_options("framerate", "30");*/
    //ipCam->set_options("vcodec", "h264");

    int ret = ipCam->open(CameraPath);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open IP camera at %s with error %s.\n", CameraPath.c_str(), ipCam->get_error_message().c_str());
        exit(1);
    }
    input_stream = ipCam->get_stream(ipCam->get_video_index());

    // Debug only, output the camera information
    if (Debug)
    {
       av_dump_format(ipCam->get_input_format_context(), 0, CameraPath.c_str(), 0);
    }

    // Open a circular buffer

    cbuf = new CircularBuffer();
    cbuf->open(120, 100 * 1000 * 1000); // set the circular buffer to be hold packets for 120s and maximum size 100M
    cbuf->add_stream(input_stream);
    Muxer* bg_recorder;

    bg_recorder= new Muxer();
    int bg_video_muxer_index = bg_recorder->add_stream(input_stream);
    Muxer* mn_recorder;
    mn_recorder = new Muxer();
    int mn_video_muxer_index = mn_recorder->add_stream(input_stream);

    // Start a seperate thread to capture video stream from the IP camera
    pthread_t thread;

    //thread_args[i] = i;
    ret = pthread_create(&thread, NULL, videoCapture, NULL);

    if (ret)
    {
        fprintf(stderr, "Cannot create the thread.");
        exit(1);
    }

    AVPacket pkt;
    AVRational timebase = cbuf->get_time_base();
    int64_t pts0 = 0;
    int64_t last_pts = 0;
    bool no_data = true;

    //bg_recorder->set_options("movflags", "frag_keyframe");
    bg_recorder->set_options("format", "mp4"); // self defined option

    // Open a chunked recording for background recording, where chunk time is 1200s
    ret = bg_recorder->open(prefix_videofile + "background", 1200);
    //av_dump_format(bg_recorder->get_output_format_context(), 0, bg_recorder->get_url().c_str(), 1);

    // Muxer* net_cam = new Muxer();
    // net_cam->set_options("format", "mpegts");
    // int net_muxer_index = net_cam->add_stream(input_stream[i]);

    //mn_recorder->set_options("movflags", "frag_keyframe");

    //  ret = net_cam->open(ip_videotrans, 3600);

    // start main recording 150s later
    int64_t MainStartTime = av_gettime() / 1000 + 150000;
    int64_t ChunkTime_mn = 0;  // Chunk time for main recording
    int64_t CurrentTime;
    while (true)
    {
        // sleep to reduce the cpu usage
        if (no_data)
        {
            av_usleep(1000 * 15); // sleep for extra 15ms when there is no more background reading
        }
        av_usleep(1000 * 5); // sleep for 5ms}

        CurrentTime = av_gettime() / 1000;  // read current time in miliseconds
        no_data = true;

        // read a background packet from the queue
        ret = cbuf->peek_packet(&pkt);
        if (ret > 0)
        {
            if (pts0 == 0)
            {
                pts0 = pkt.pts;
                last_pts = pkt.pts;
                if (Debug > 1)
                {
                    fprintf(stderr, "The first packet: pts=%lld, pts_time=%lld \n",
                        pts0, pts0 * timebase.num / timebase.den);
                }
            }

            if (Debug > 2)
            {
                fprintf(stderr, "Read a background packet pts time: %lldms, dt: %lldms, packet size %d, total packets: %d.\n",
                    1000 * pkt.pts * timebase.num / timebase.den,
                    1000 * (pkt.pts - pts0) * timebase.num / timebase.den, pkt.size, cbuf->get_total_packets());
            }

            if (pkt.pts == AV_NOPTS_VALUE || pkt.size == 0 || pkt.pts < last_pts)
            {
                fprintf(stderr, "Read a wrong background packet pts time: %lldms, dt: %lldms, packet size %d, total size: %d.\n",
                    1000 * pkt.pts * timebase.num / timebase.den,
                    1000 * (pkt.pts - pts0) * timebase.num / timebase.den, pkt.size, cbuf->get_total_packets());
            }

            last_pts = pkt.pts;
            ret = bg_recorder->record(&pkt);
            // check for error
            if (ret < 0)
            {
                fprintf(stderr, "%s muxing packet (%lldms, %d) in %s.\n",
                    bg_recorder->get_error_message().c_str(),
                    1000 * (pkt.pts - pts0) * timebase.num / timebase.den, pkt.size,
                    bg_recorder->get_url().c_str());
                break;
            }

            // check for chunk
            if (ret > 0)
            {
                av_dump_format(bg_recorder->get_output_format_context(), 0,
                    bg_recorder->get_url().c_str(), 1);
            }
            no_data = false;
        }

        // arbitrary set main recording starts 15s later
        if (CurrentTime <= MainStartTime)
        {
            continue;
        }

        if (!ChunkTime_mn)
        {
            ret = mn_recorder->open(prefix_videofile + "main-", 3600);
            av_dump_format(mn_recorder->get_output_format_context(), 0, mn_recorder->get_url().c_str(), 1);
            ChunkTime_mn = CurrentTime + 1200000;
        }

        // simulate the external chunk signal
        if (CurrentTime > ChunkTime_mn)
        {
            ChunkTime_mn += 1200000;
            mn_recorder->chunk();
            av_dump_format(mn_recorder->get_output_format_context(), 0, mn_recorder->get_url().c_str(), 1);
            fprintf(stderr, "Main recording get chunked.\n");
        }

        // handle the main stream reading
        ret = cbuf->peek_packet(&pkt, false);
        if (ret > 0)
        {
            if (Debug)
            {
                fprintf(stderr, "Read a main packet pts time: %lld, dt: %lldms, packet size %d, total size: %d.\n",
                    pkt.pts * timebase.num / timebase.den,
                    1000 * (pkt.pts - pts0) * timebase.num / timebase.den, pkt.size, ret);
            }

            if (mn_recorder->record(&pkt) < 0)
            {
                fprintf(stderr, "%s muxing packet in %s.\n",
                    mn_recorder->get_error_message().c_str(),
                    mn_recorder->get_url().c_str());
                break;
            }
            //av_packet_unref(&pkt);

            no_data = false;
        }
    }

    if (ret < 0)
        fprintf(stderr, " with error %s.\n", av_err(ret));
}


