#include "rtmp_pusher.h"
#include <exception>

using namespace std;

rtmp_pusher::rtmp_pusher(
	QString output,
	int width,
	int height,
	int fps,
	int bitrate,
	int gop_size,
	int max_b_frames,
	bool log_enable,
	QObject* parent)
	: QThread(parent)
{
	this->width = width;
	this->height = height;
	this->fps = fps;
	this->bitrate = bitrate;
	this->gop_size = gop_size;
	this->max_b_frames = max_b_frames;
	this->output = output;
	this->log_enable = log_enable;
	if (log_enable)
	{
		av_log_set_level(AV_LOG_DEBUG);
	}
}

rtmp_pusher::~rtmp_pusher()
{
	requestInterruption();
	quit();
	wait();
}

void rtmp_pusher::frame_ready(cv::Mat frame)
{
	QMutexLocker lock(&mtx);
	frames.push_back(frame);
}


void rtmp_pusher::run()
{
	while (!isInterruptionRequested())
	{
		//const char *outUrl = "udp://192.168.108.133:12345";
		//const char *outUrl = "udp://239.238.10.10:12345";
		//const char *outUrl = "rtmp://152.136.187.230:1999/hls/m";
		//const char *outUrl = "rtsp://192.168.108.133:554/live/stream.sdp";		
		QByteArray byUrl = output.toLatin1();
		const char* outUrl = byUrl.data();
		avcodec_register_all();	//ע�����еı������
		av_register_all();		//ע�����еķ�װ��
		avformat_network_init();//ע����������Э��
		SwsContext *vsc = NULL;		//���ظ�ʽת��������
		AVFrame *yuv = NULL;		//��������ݽṹ
		AVCodecContext *vc = NULL;	//������������
		AVFormatContext *ic = NULL;
		
		cv::Mat frame;
		try	
		{   
			//��ʼ����ʽת��������
			vsc = sws_getCachedContext(vsc,
				width, height, AV_PIX_FMT_BGR24,  //Դ     ���ߡ����ظ�ʽ
				width, height, AV_PIX_FMT_YUV420P,//Ŀ��   ���ߡ����ظ�ʽ
				SWS_BICUBIC,  //�ߴ�仯ʹ���㷨
				0, 0, 0);
			if (!vsc) {
				throw exception("sws_getCachedContext failed!");
			}
			// ��ʼ����������ݽṹ
			yuv = av_frame_alloc();
			yuv->format = AV_PIX_FMT_YUV420P;
			yuv->width = width;
			yuv->height = height;
			yuv->pts = 0;
			int ret = av_frame_get_buffer(yuv, 32);
			if (ret != 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, sizeof(buf) - 1);
				throw exception(buf);
			}

			// ��ʼ������������
			//a �ҵ�������
			AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
			if (!codec)	{
				throw exception("Can`t find h264 encoder!");
			}
			//b ����������������
			vc = avcodec_alloc_context3(codec);
			if (!vc) {
				throw exception("avcodec_alloc_context3 failed!");
			}
			//c ���ñ���������
			vc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; //ȫ�ֲ���
			vc->codec_id = codec->id;
			vc->thread_count = 8;

			vc->bit_rate = bitrate;//ѹ����ÿ����Ƶ��bitλ��С 50kB
			vc->width = width;
			vc->height = height;
			vc->time_base = { 1, fps };
			vc->framerate = { fps, 1 };

			vc->gop_size = gop_size;//������Ĵ�С������֡һ���ؼ�֡
			vc->max_b_frames = max_b_frames;
			vc->pix_fmt = AV_PIX_FMT_YUV420P;
			//d �򿪱�����������
			ret = avcodec_open2(vc, 0, 0);
			if (ret != 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, sizeof(buf) - 1);
				throw exception(buf);
			}
			//cout << "avcodec_open2 success!" << endl;

			// �����װ������Ƶ������
			//a ���������װ��������			
			//ret = avformat_alloc_output_context2(&ic, 0, "rtsp", outUrl);//rtsp			
			//av_opt_set(ic->priv_data, "rtsp_transport", "tcp", 0);//ʹ��tcpЭ�鴫��			
			//ret = avformat_alloc_output_context2(&ic, 0, "mpegts", outUrl);//udp			
			ret = avformat_alloc_output_context2(&ic, 0, "flv", outUrl);//rtmp
			if (ret != 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, sizeof(buf) - 1);
				throw exception(buf);
			}
			//b �����Ƶ�� 
			AVStream *vs = avformat_new_stream(ic, NULL);
			if (!vs) {
				throw exception("avformat_new_stream failed");
			}		
			vs->codecpar->codec_tag = 0;			
			avcodec_parameters_from_context(vs->codecpar, vc);//�ӱ��������Ʋ���
			av_dump_format(ic, 0, outUrl, 1);
			///��rtmp ���������IO
			ret = avio_open(&ic->pb, outUrl, AVIO_FLAG_WRITE);
			if (ret != 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, sizeof(buf) - 1);
				throw exception(buf);
			}
			//cout << "avio_open success!" << endl;
			ret = avformat_write_header(ic, NULL);//д���װͷ
			if (ret != 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, sizeof(buf) - 1);
				throw exception(buf);
			}

			AVPacket pack;
			memset(&pack, 0, sizeof(pack));
			int vpts = 0;
			while (true)
			{
				{//��ȡ֡����
					QMutexLocker lock(&mtx);
					if (frames.isEmpty()) {
						QThread::msleep(10);
						continue;
					}
					frame = frames.takeFirst();
				}

				///rgb to yuv
				//��������ݽṹ
				uint8_t *indata[AV_NUM_DATA_POINTERS] = { 0 };
				indata[0] = frame.data;
				int insize[AV_NUM_DATA_POINTERS] = { 0 };				
				insize[0] = frame.cols * frame.elemSize();//һ�У������ݵ��ֽ���
				int h = sws_scale(vsc, indata, insize, 0, frame.rows, //Դ����
					yuv->data, yuv->linesize);
				if (h <= 0) {
					continue;
				}
				//cout << h << " " << flush;
				
				//h264����
				yuv->pts = vpts;
				vpts++;
				ret = avcodec_send_frame(vc, yuv);
				if (ret != 0) {
					cout << "avcodec_send_frame() error." << std::endl;
					continue;
				}

				ret = avcodec_receive_packet(vc, &pack);
				if (ret != 0 || pack.size > 0) {
					//cout << "*" << pack.size << flush;
				}
				else {
					cout << "avcodec_receive_packet() error." << std::endl;
					continue;
				}

				//����
				pack.pts = av_rescale_q(pack.pts, vc->time_base, vs->time_base);
				pack.dts = av_rescale_q(pack.dts, vc->time_base, vs->time_base);
				pack.duration = av_rescale_q(pack.duration, vc->time_base, vs->time_base);
				ret = av_interleaved_write_frame(ic, &pack);
				if (ret != 0) {
					cout << "av_interleaved_write_frame() error." << std::endl;
					continue;
				}
			}
		}
		catch (exception &ex)
		{
			if (vsc) {
				sws_freeContext(vsc);
				vsc = NULL;
			}

			if (vc)	{
				avio_closep(&ic->pb);
				avcodec_free_context(&vc);
			}

			cerr << ex.what() << endl;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///�ο�����
#if 0
#include <opencv2/highgui.hpp>
#include <iostream>
extern "C"
{
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avformat.lib")
//#pragma comment(lib,"opencv_world300.lib")
using namespace std;
using namespace cv;
int main(int argc, char *argv[])
{
	//���������rtsp url
	char *inUrl = "rtsp://test:test123456@192.168.1.64";
	//nginx-rtmp ֱ��������rtmp����URL
	char *outUrl = "rtmp://192.168.1.101/live";

	//ע�����еı������
	avcodec_register_all();

	//ע�����еķ�װ��
	av_register_all();

	//ע����������Э��
	avformat_network_init();


	VideoCapture cam;
	Mat frame;
	namedWindow("video");

	//���ظ�ʽת��������
	SwsContext *vsc = NULL;

	//��������ݽṹ
	AVFrame *yuv = NULL;

	//������������
	AVCodecContext *vc = NULL;

	//rtmp flv ��װ��                                                           
	AVFormatContext *ic = NULL;


	try
	{   ////////////////////////////////////////////////////////////////
		/// 1 ʹ��opencv��rtsp���
		cam.open(inUrl);
		//cam.open(0); // �������
		if (!cam.isOpened())
		{
			throw exception("cam open failed!");
		}
		cout << inUrl << " cam open success" << endl;
		int inWidth = cam.get(CAP_PROP_FRAME_WIDTH);
		int inHeight = cam.get(CAP_PROP_FRAME_HEIGHT);
		int fps = cam.get(CAP_PROP_FPS);
		fps = 25;

		///2 ��ʼ����ʽת��������
		vsc = sws_getCachedContext(vsc,
			inWidth, inHeight, AV_PIX_FMT_BGR24,     //Դ���ߡ����ظ�ʽ
			inWidth, inHeight, AV_PIX_FMT_YUV420P,//Ŀ����ߡ����ظ�ʽ
			SWS_BICUBIC,  // �ߴ�仯ʹ���㷨
			0, 0, 0
		);
		if (!vsc)
		{
			throw exception("sws_getCachedContext failed!");
		}
		///3 ��ʼ����������ݽṹ
		yuv = av_frame_alloc();
		yuv->format = AV_PIX_FMT_YUV420P;
		yuv->width = inWidth;
		yuv->height = inHeight;
		yuv->pts = 0;
		//����yuv�ռ�
		int ret = av_frame_get_buffer(yuv, 32);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}

		///4 ��ʼ������������
		//a �ҵ�������
		AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!codec)
		{
			throw exception("Can`t find h264 encoder!");
		}
		//b ����������������
		vc = avcodec_alloc_context3(codec);
		if (!vc)
		{
			throw exception("avcodec_alloc_context3 failed!");
		}
		//c ���ñ���������
		vc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; //ȫ�ֲ���
		vc->codec_id = codec->id;
		vc->thread_count = 8;

		vc->bit_rate = 50 * 1024 * 8;//ѹ����ÿ����Ƶ��bitλ��С 50kB
		vc->width = inWidth;
		vc->height = inHeight;
		vc->time_base = { 1,fps };
		vc->framerate = { fps,1 };

		//������Ĵ�С������֡һ���ؼ�֡
		vc->gop_size = 50;
		vc->max_b_frames = 0;
		vc->pix_fmt = AV_PIX_FMT_YUV420P;
		//d �򿪱�����������
		ret = avcodec_open2(vc, 0, 0);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}
		cout << "avcodec_open2 success!" << endl;

		///5 �����װ������Ƶ������
		//a ���������װ��������
		ret = avformat_alloc_output_context2(&ic, 0, "flv", outUrl);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}
		//b �����Ƶ�� 
		AVStream *vs = avformat_new_stream(ic, NULL);
		if (!vs)
		{
			throw exception("avformat_new_stream failed");
		}
		vs->codecpar->codec_tag = 0;
		//�ӱ��������Ʋ���
		avcodec_parameters_from_context(vs->codecpar, vc);
		av_dump_format(ic, 0, outUrl, 1);


		///��rtmp ���������IO
		ret = avio_open(&ic->pb, outUrl, AVIO_FLAG_WRITE);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}

		//д���װͷ
		ret = avformat_write_header(ic, NULL);
		if (ret != 0)
		{
			char buf[1024] = { 0 };
			av_strerror(ret, buf, sizeof(buf) - 1);
			throw exception(buf);
		}

		AVPacket pack;
		memset(&pack, 0, sizeof(pack));
		int vpts = 0;
		for (;;)
		{
			///��ȡrtsp��Ƶ֡��������Ƶ֡
			if (!cam.grab())
			{
				continue;
			}
			///yuvת��Ϊrgb
			if (!cam.retrieve(frame))
			{
				continue;
			}
			imshow("video", frame);
			waitKey(20);


			///rgb to yuv
			//��������ݽṹ
			uint8_t *indata[AV_NUM_DATA_POINTERS] = { 0 };
			//indata[0] bgrbgrbgr
			//plane indata[0] bbbbb indata[1]ggggg indata[2]rrrrr 
			indata[0] = frame.data;
			int insize[AV_NUM_DATA_POINTERS] = { 0 };
			//һ�У������ݵ��ֽ���
			insize[0] = frame.cols * frame.elemSize();
			int h = sws_scale(vsc, indata, insize, 0, frame.rows, //Դ����
				yuv->data, yuv->linesize);
			if (h <= 0)
			{
				continue;
			}
			//cout << h << " " << flush;
			///h264����
			yuv->pts = vpts;
			vpts++;
			ret = avcodec_send_frame(vc, yuv);
			if (ret != 0)
				continue;

			ret = avcodec_receive_packet(vc, &pack);
			if (ret != 0 || pack.size > 0)
			{
				//cout << "*" << pack.size << flush;
			}
			else
			{
				continue;
			}
			//����
			pack.pts = av_rescale_q(pack.pts, vc->time_base, vs->time_base);
			pack.dts = av_rescale_q(pack.dts, vc->time_base, vs->time_base);
			pack.duration = av_rescale_q(pack.duration, vc->time_base, vs->time_base);
			ret = av_interleaved_write_frame(ic, &pack);
			if (ret == 0)
			{
				cout << "#" << flush;
			}
		}

	}
	catch (exception &ex)
	{
		if (cam.isOpened())
			cam.release();
		if (vsc)
		{
			sws_freeContext(vsc);
			vsc = NULL;
		}

		if (vc)
		{
			avio_closep(&ic->pb);
			avcodec_free_context(&vc);
		}

		cerr << ex.what() << endl;
	}
	getchar();
	return 0;
}
#endif