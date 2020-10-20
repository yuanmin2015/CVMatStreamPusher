#include "video_cap.h"
#include <QTimer>
#include <QDebug>

video_cap::video_cap(QObject *parent)
	: QObject(parent)
{
	timer_video = Q_NULLPTR;
	timer_reconn = Q_NULLPTR;
	connect(this, &video_cap::signal_set_timer, this, &video_cap::slot_set_timer);

	start_thread();
}

video_cap::~video_cap()
{
	stop_thread();

	if (timer_video != Q_NULLPTR)
		delete timer_video;
	if (timer_reconn != Q_NULLPTR)
		delete timer_reconn;
}


void video_cap::start_thread()
{
	thread = new QThread();
	//connect(thread, &QThread::finished, thread, &QThread::deleteLater);

	moveToThread(thread);
	thread->start();
}

void video_cap::stop_thread()
{
	thread->quit();
	thread->wait();

	if (thread != Q_NULLPTR)
		delete thread;
}

bool video_cap::open(QString video_url, int& width, int& height, int& fps)
{
	if (video_url.isEmpty())
	{
		qCritical("(video cap)Video url is empty.");
		return false;
	}

	url_ = video_url;
	if (!cap.open(cv::String(url_.toUtf8().data())))
	{
		qCritical("(video cap)Open video url failed, url:%s", url_.toUtf8().data());
		return false;
	}

	width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
	height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
	fps = cap.get(CV_CAP_PROP_FPS);
	
	//timer_video->start();
	emit signal_set_timer();
	qInfo("(video cap)Open video url successfully, url->%s, width->%d, height->%d, fps->%d.",
		url_.toUtf8().data(),
		width,
		height,
		fps);
	return true;
}

void video_cap::slot_read_video()
{
	if (!cap.isOpened())
	{
		timer_video->stop();
		timer_reconn->start();
		return;
	}
	cv::Mat frame;
	cap >> frame;
	if (frame.empty())
	{
		nfdc++;
		qWarning("(video cap)There is no data in frame, nfdc:%d.", nfdc);
		if (nfdc > 9)
		{//连续10次未读到数据，进行重连
			timer_video->stop();
			timer_reconn->start();
			nfdc = 0;
		}

		return;
	}
	nfdc = 0;//重置计数
	//qInfo("(video cap)width->%d, height->%d.", frame.cols, frame.rows);
	//cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
	//cv::cvtColor(frame, frame, cv::COLOR_BGR2YUV_I420);
	emit frame_received(frame);

	cv::imshow("raw", frame);
	cv::waitKey(10);
}

void video_cap::slot_reconnecting()
{//文件或视频流读取失败重连
	qInfo("(video cap)Video reconnecting...");
	cap.release();//清理cap资源，重新打开
	if (cap.open(cv::String(url_.toUtf8().data())))
	{
		timer_video->start();
		timer_reconn->stop();
		qInfo("(video cap)Video connected.");
	}
}

void video_cap::slot_set_timer()
{
	if (timer_video != Q_NULLPTR)
		delete timer_video;
	if (timer_reconn != Q_NULLPTR)
		delete timer_reconn;

	//读视频帧定时器
	timer_video = new QTimer();
	connect(timer_video, &QTimer::timeout, this, &video_cap::slot_read_video);
	timer_video->setInterval(10);
	timer_video->moveToThread(thread);

	//重连定时器
	timer_reconn = new QTimer();
	connect(timer_reconn, &QTimer::timeout, this, &video_cap::slot_reconnecting);
	timer_reconn->setInterval(5000);
	timer_reconn->moveToThread(thread);
	timer_video->start();
}
