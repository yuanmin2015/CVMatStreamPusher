#include "rtsp_pusher.h"
#include <exception>

using namespace std;

rtsp_pusher::rtsp_pusher(
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

rtsp_pusher::~rtsp_pusher()
{
	requestInterruption();
	quit();
	wait();
}

void rtsp_pusher::frame_ready(cv::Mat frame)
{
	QMutexLocker lock(&mtx);
	frames.push_back(frame);
}


void rtsp_pusher::run()
{
	while (!isInterruptionRequested())
	{
		//const char *outUrl = "udp://192.168.108.133:12345";
		//const char *outUrl = "udp://239.238.10.10:12345";
		//const char *outUrl = "rtmp://152.136.187.230:1999/hls/m";
		//const char *outUrl = "rtsp://192.168.108.133:554/live/stream.sdp";
		QByteArray byUrl = output.toLatin1();
		const char* outUrl = byUrl.data();
		//qInfo("url->%s", outUrl);
		avcodec_register_all();	//注册所有的编解码器
		av_register_all();		//注册所有的封装器
		avformat_network_init();//注册所有网络协议
		SwsContext *vsc = NULL;		//像素格式转换上下文
		AVFrame *yuv = NULL;		//输出的数据结构
		AVCodecContext *vc = NULL;	//编码器上下文
		AVFormatContext *ic = NULL;

		cv::Mat frame;
		try
		{
			//初始化格式转换上下文
			vsc = sws_getCachedContext(vsc,
				width, height, AV_PIX_FMT_BGR24,  //源     宽、高、像素格式
				width, height, AV_PIX_FMT_YUV420P,//目标   宽、高、像素格式
				SWS_BICUBIC,  //尺寸变化使用算法
				0, 0, 0);
			if (!vsc) {
				throw exception("sws_getCachedContext failed!");
			}
			// 初始化输出的数据结构
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

			// 初始化编码上下文
			//a 找到编码器
			AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
			if (!codec) {
				throw exception("Can`t find h264 encoder!");
			}
			//b 创建编码器上下文
			vc = avcodec_alloc_context3(codec);
			if (!vc) {
				throw exception("avcodec_alloc_context3 failed!");
			}
			//c 配置编码器参数
			vc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; //全局参数
			vc->codec_id = codec->id;
			vc->thread_count = 8;

			vc->bit_rate = bitrate;//压缩后每秒视频的bit位大小 50kB
			vc->width = width;
			vc->height = height;
			vc->time_base = { 1, fps };
			vc->framerate = { fps, 1 };

			vc->gop_size = gop_size;//画面组的大小，多少帧一个关键帧
			vc->max_b_frames = max_b_frames;
			vc->pix_fmt = AV_PIX_FMT_YUV420P;
			//d 打开编码器上下文
			ret = avcodec_open2(vc, 0, 0);
			if (ret != 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, sizeof(buf) - 1);
				throw exception(buf);
			}
			//cout << "avcodec_open2 success!" << endl;

			// 输出封装器和视频流配置
			//a 创建输出封装器上下文			
			ret = avformat_alloc_output_context2(&ic, 0, "rtsp", outUrl);//rtsp			
			av_opt_set(ic->priv_data, "rtsp_transport", "tcp", 0);//使用tcp协议传输			
			//ret = avformat_alloc_output_context2(&ic, 0, "mpegts", outUrl);//udp			
			//ret = avformat_alloc_output_context2(&ic, 0, "flv", outUrl);//rtmp
			if (ret != 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, sizeof(buf) - 1);
				throw exception(buf);
			}
			//b 添加视频流 
			AVStream *vs = avformat_new_stream(ic, NULL);
			if (!vs) {
				throw exception("avformat_new_stream failed");
			}
			vs->codecpar->codec_tag = 0;
			avcodec_parameters_from_context(vs->codecpar, vc);//从编码器复制参数
			av_dump_format(ic, 0, outUrl, 1);

			/**
			* Unlike RTMP defined as a pure protocol based on FLV, RTSP is a format and a protocol meanwhile.
			* FFmpeg will automatically create the io context when allocating output context, so you don't need to call avio_open manually anymore.
		    * Just comment avio_open, and it should work fine.
			*/
			//ret = avio_open(&ic->pb, outUrl, AVIO_FLAG_WRITE);
			//if (ret != 0) {
			//	char buf[1024] = { 0 };
			//	av_strerror(ret, buf, sizeof(buf) - 1);
			//	throw exception(buf);
			//}
			//cout << "avio_open success!" << endl;
			ret = avformat_write_header(ic, NULL);//写入封装头
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
				{//获取帧数据
					QMutexLocker lock(&mtx);
					if (frames.isEmpty()) {
						QThread::msleep(10);
						continue;
					}
					frame = frames.takeFirst();
				}

				///rgb to yuv
				//输入的数据结构
				uint8_t *indata[AV_NUM_DATA_POINTERS] = { 0 };
				indata[0] = frame.data;
				int insize[AV_NUM_DATA_POINTERS] = { 0 };
				insize[0] = frame.cols * frame.elemSize();//一行（宽）数据的字节数
				int h = sws_scale(vsc, indata, insize, 0, frame.rows, //源数据
					yuv->data, yuv->linesize);
				if (h <= 0) {
					continue;
				}
				//cout << h << " " << flush;

				//h264编码
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

				//推流
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

			if (vc) {
				avio_closep(&ic->pb);
				avcodec_free_context(&vc);
			}

			cerr << ex.what() << endl;
		}
	}
}