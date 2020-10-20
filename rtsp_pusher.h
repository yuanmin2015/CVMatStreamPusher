#ifndef RTSP_PUSHER_H_
#define RTSP_PUSHER_H_
#include <QObject>
#include <QDebug>
#include <QThread>
#include <QMutex>
#include <QList>
#include <QString>

#include <iostream>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/video.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>	
}

//输出rtsp流与udp或者rtmp不同，故特别定义
//#ifndef OUTPUT_RTSP
//#define OUTPUT_RTSP
//#endif

class rtsp_pusher : public QThread
{
	Q_OBJECT
public:
	//bitrate-> stream bitrate in bit/s (default: 50 * 1024 * 8 = 409600)
	//gop_size->I frame interval
	//max_b_frames->B frame interval
	//server->output RTMP server (default: rtmp://localhost/live/stream)
	//log_enable->print debug output of ffmpeg (default: false)
	explicit rtsp_pusher(
		QString output,
		int width,
		int height,
		int fps,
		int bitrate = 409600,
		int gop_size = 50,
		int max_b_frames = 0,
		bool log_enable = false,
		QObject *parent = nullptr);
	~rtsp_pusher();

public slots:
	void frame_ready(cv::Mat frame);

protected:
	void run() override;

private:
	QList<cv::Mat> frames;
	QMutex mtx;

	int width;
	int height;
	int fps;
	int bitrate;
	int gop_size;
	int max_b_frames;
	QString output;
	bool log_enable;
};
#endif