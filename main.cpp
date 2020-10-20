#include <QtCore/QCoreApplication>

#include "rtmp_pusher.h"
#include "rtsp_pusher.h"
#include "udp_pusher.h"
#include "video_cap.h"

#include "opencv2/opencv.hpp"

int main(int argc, char *argv[])
{
	qRegisterMetaType<cv::Mat>("cv::Mat");
    QCoreApplication a(argc, argv);
//////////////////////TEST CODE
	video_cap* vcap = new video_cap();
	int width = 0, height = 0 , fps = 0;

	if (vcap->open("./5.wmv", width, height, fps))
	{
		//rtmp_pusher* pusher_a = new rtmp_pusher("rtmp://192.168.108.133/live", width, height, fps, 50 * 1024 * 8, 50, 0, true);
		//QObject::connect(vcap, &video_cap::frame_received, pusher_a, &rtmp_pusher::frame_ready);
		//pusher_a->start();

		rtsp_pusher* pusher_b = new rtsp_pusher("rtsp://192.168.108.133/stream1", width, height, fps, 800 * 1024 * 8, 50, 0, true);
		QObject::connect(vcap, &video_cap::frame_received, pusher_b, &rtsp_pusher::frame_ready);
		pusher_b->start();

		//udp_pusher* pusher_c = new udp_pusher("udp://239.56.78.109:20000", width, height, fps, 50 * 1024 * 8, 50, 0, true);
		//QObject::connect(vcap, &video_cap::frame_received, pusher_c, &udp_pusher::frame_ready);
		//pusher_c->start();
	}
	//vcap->open("rtsp://admin:admin12345@192.168.255.245:554/h264/ch1/main/av_stream", width, height, fps)
	//dev->open("rtsp://192.168.1.200:554/rdr?channel=1");
	//dev->open("./3.wmv");

    return a.exec();
}
