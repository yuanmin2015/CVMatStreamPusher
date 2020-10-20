/*
To get video data from rtsp stream or local file, then encoding with h.264

*/
#ifndef VIDEO_CAP_H_
#define VIDEO_CAP_H_

#include <QObject>
#include <QThread>
#include <QTimer>

#include "opencv2/opencv.hpp"

class video_cap : public QObject
{
	Q_OBJECT

public:
	video_cap(QObject *parent = nullptr);
	~video_cap();
	//初始化成功后，自动发送编码好的视频帧数据
	bool open(QString video_url, int& width, int& height, int& fps);
private:
	void start_thread();
	void stop_thread();

	public slots:
	void slot_read_video();
	void slot_reconnecting();
	void slot_set_timer();

signals:
	void frame_received(cv::Mat frame);	
	void signal_set_timer();

private:
	QTimer* timer_video;
	QTimer* timer_reconn;
	QString url_;//文件路径或者视频流地址
	cv::VideoCapture cap;
	//cv::Mat frame;
	int nfdc;//no frame data count
	QThread* thread;
};
#endif