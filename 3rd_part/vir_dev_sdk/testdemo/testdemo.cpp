#include "SharedBufferClientInterface.h"

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include <string>
#include <thread>
#include <dlfcn.h>

#include <android/log.h>
#ifdef LOG_TAG
    #undef LOG_TAG
#endif
#define LOG_TAG "VirDevTestDemo"

#define PRINT_MOD_A(level, format, arg...) do {\
    __android_log_print(level, LOG_TAG, "[%s,%d] " format, __FUNCTION__,__LINE__, ##arg);\
} while(0)

#define LOG_V(...) PRINT_MOD_A(ANDROID_LOG_VERBOSE, __VA_ARGS__)
#define LOG_D(...) PRINT_MOD_A(ANDROID_LOG_DEBUG, __VA_ARGS__)
#define LOG_I(...) PRINT_MOD_A(ANDROID_LOG_INFO,  __VA_ARGS__)
#define LOG_W(...) PRINT_MOD_A(ANDROID_LOG_WARN,  __VA_ARGS__)
#define LOG_E(...) PRINT_MOD_A(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOG_F(...) PRINT_MOD_A(ANDROID_LOG_FATAL, __VA_ARGS__)

#define ALOGV(...) PRINT_MOD_A(ANDROID_LOG_VERBOSE, __VA_ARGS__)
#define ALOGD(...) PRINT_MOD_A(ANDROID_LOG_DEBUG, __VA_ARGS__)
#define ALOGI(...) PRINT_MOD_A(ANDROID_LOG_INFO,  __VA_ARGS__)
#define ALOGW(...) PRINT_MOD_A(ANDROID_LOG_WARN,  __VA_ARGS__)
#define ALOGE(...) PRINT_MOD_A(ANDROID_LOG_ERROR, __VA_ARGS__)
#define ALOGF(...) PRINT_MOD_A(ANDROID_LOG_FATAL, __VA_ARGS__)


static bool g_loop = true;

static uint8_t g_micStatus = 0;
static uint8_t g_camStatus = 0;

static size_t g_cameraFrameSize = 0;
static size_t g_cameraWidth = 0;
static size_t g_cameraHeight = 0;
static int g_cameraFrameRate = 0;

static int g_micFrameRate = 0;

static bool g_MicrophoneCrash = false;
static bool g_CameraCrash = false;

class NotifyCallbackListenerImpl : public NotifyCallbackListener {
public:
    NotifyCallbackListenerImpl() {}
    ~NotifyCallbackListenerImpl() override {}

    void onNotifyCallback(uint8_t msgType, uint8_t status, int32_t val1, int32_t val2, int32_t val3) override {
        ALOGD("msgType=%d, status=%d, val1=%d, val2=%d, val3=%d", msgType, status, val1, val2, val3);

		// 此时，应该通知真实摄像头或真实麦克风，打开设备
		// 比如: 创建一个 Message 再 sendMessage 到其他线程处理。切记：千万不要阻塞回调线程，以防止意想不到的问题。
		// Message m = mEventHandler.obtainMessage(msgType, status, val1, val2, val3);
		// mEventHandler.sendMessage(m);
		// return;

		// 以下仅为测试代码
		if (msgType == VirtualDeviceType::kVirCamera) {
			g_camStatus = status;
			g_cameraWidth = val1;
			g_cameraHeight = val2;
			g_cameraFrameSize = val1 * val2 * 3 / 2;
			g_cameraFrameRate = val3;
		} else if (msgType == VirtualDeviceType::kVirMicrophone) {
			g_micStatus = status;
			g_micFrameRate = val1 / val3;
		} else if (msgType == VirtualDeviceType::kVirDevicesCrash) {
			if (status == VirtualDeviceType::kVirCamera) {
				ALOGE("error: The camera may have crashed");
				g_CameraCrash = true;
			} else if (status == VirtualDeviceType::kVirMicrophone) {
				ALOGE("error: The camera audio have crashed");
				g_MicrophoneCrash =  true;
			}
		}
    }
};


// 当打开虚拟摄像头或虚拟麦克风时，会回调该函数
static void onNotifyCallbackTest(uint8_t msgType, uint8_t status, int32_t val1, int32_t val2, int32_t val3)
{
    ALOGD("msgType=%d, status=%d, val1=%d, val2=%d, val3=%d", msgType, status, val1, val2, val3);

	// 此时，应该通知真实摄像头或真实麦克风，打开设备
	// 比如: 创建一个 Message 再 sendMessage 到其他线程处理。切记：千万不要阻塞回调线程，以防止意想不到的问题。
	// Message m = mEventHandler.obtainMessage(msgType, status, val1, val2, val3);
	// mEventHandler.sendMessage(m);
	// return;

	// 以下仅为测试代码
	if (msgType == VirtualDeviceType::kVirCamera) {
		g_camStatus = status;
		g_cameraWidth = val1;
		g_cameraHeight = val2;
		g_cameraFrameSize = val1 * val2 * 3 / 2;
		g_cameraFrameRate = val3;
	} else if (msgType == VirtualDeviceType::kVirMicrophone) {
		g_micStatus = status;
		g_micFrameRate = val1 / val3;
	} else if (msgType == VirtualDeviceType::kVirDevicesCrash) {
		if (status == VirtualDeviceType::kVirCamera) {
			ALOGE("error: The camera may have crashed");
			g_CameraCrash = true;
		} else if (status == VirtualDeviceType::kVirMicrophone) {
			ALOGE("error: The camera audio have crashed");
			g_MicrophoneCrash =  true;
		}
	}
}


static void debugShowFPS(const char *tag, int &mFrameCount, int &mLastFrameCount, uint64_t &mLastFpsTime, float mFps)
{
#if 0
    static int mFrameCount;
    static int mLastFrameCount = 0;
    static uint64_t mLastFpsTime = 0;
    static float mFps = 0;
#endif

    mFrameCount++;
 	struct timeval tv;
 	gettimeofday(&tv, NULL);
	uint64_t now = ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;
    uint64_t diff = (now - mLastFpsTime);
    if (diff > 500) {
        mFps = ((mFrameCount - mLastFrameCount) * 1000) / diff;
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
        ALOGD("%s: input fps: %2.3f", tag, mFps);
    }
}

// 控制输入帧率
static void controlFrameRate(int64_t &last_read_time_us, int framerate)
{
#if 0
	static int64_t last_read_time_us = 0;
	static int period = 0;
#else
	int period = 0;
#endif

	// if (period == 0) {
		period = 1000000 / framerate;
	// }

	struct timeval tv;
	gettimeofday(&tv, NULL);
	int64_t now_us = ((uint64_t)tv.tv_sec) * 1000000 + ((uint64_t)tv.tv_usec);

	const bool standby = (last_read_time_us == 0);
	const int64_t elapsed_time_since_last_read = standby ? 0 : now_us - last_read_time_us;

	int64_t sleep_time_us = period - elapsed_time_since_last_read;
	if (sleep_time_us > 0) {
		usleep(sleep_time_us);
	} else {
		sleep_time_us = 0;
	}
	
	last_read_time_us = now_us + sleep_time_us;
}

int testCamera(const char *fileName1, const char *fileName2, const char *fileName3)
{
	int ret = 0;
	constexpr size_t maxFrameSize = 1920 * 1080 * 3 / 2;

	ALOGD("test camera");

	uint8_t* yuvBuf = new uint8_t[maxFrameSize];
	if (yuvBuf == nullptr) {
		ALOGE("new buffer failed");
		return -1;
	}

 	FILE *backVideoFileHandle_720p = fopen(fileName1, "rb");
	if (backVideoFileHandle_720p == nullptr) {
		printf("open %s failed\n", fileName1);
		return -1;
	}

 	FILE *frontVideoFileHandle_720p = fopen(fileName2, "rb");
	if (frontVideoFileHandle_720p == nullptr) {
		printf("open %s failed\n", fileName2);
		return -1;
	}

 	FILE *videoFileHandle_480p = fopen(fileName3, "rb");
	if (frontVideoFileHandle_720p == nullptr) {
		printf("open %s failed\n", fileName3);
		return -1;
	}

	SharedBufferClientInterface* sharedBufferClient = createSharedBufferClient(VirtualDeviceType::kVirCamera);
    if (sharedBufferClient == nullptr) {
		ALOGE("createSharedBufferClient failed");
        return -1;
    }

	ret = sharedBufferClient->connect();
	if (ret < 0) {
		releaseSharedBufferClient(sharedBufferClient);
		ALOGE("connect shared buffer failed");
		return -1;
	}

	// 测试接口 (非必须调用)
	uint8_t status = sharedBufferClient->getVirDeviceStatus();
	ALOGD("get vir camera status: %d", status);

	uint32_t val1 = 0;
	uint32_t val2 = 0;
	uint32_t val3 = 0;
	// 测试接口 (非必须调用)
	ret = sharedBufferClient->getVirDeviceInfo(status, val1, val2, val3);
	if (ret >= 0) {
		ALOGD("get vir camera info: %d, %d, %d, %d", status, val1, val2, val3);
	}

	// 注册回调 (调用一种即可)
#if 0
	sharedBufferClient->setNotifyCallback([&](uint8_t msgType, uint8_t status, int32_t val1, int32_t val2, int32_t val3) {
		ALOGD("msgType=%d, status=%d, val1=%d, val2=%d, val3=%d", msgType, status, val1, val2, val3);
		// 此时，应该通知真实摄像头或真实麦克风，打开设备
		// 比如: 创建一个 Message 再 sendMessage 到其他线程处理。切记：千万不要阻塞回调线程，以防止意想不到的问题。
		// Message m = mEventHandler.obtainMessage(msgType, status, val1, val2, val3);
		// mEventHandler.sendMessage(m);
		// return;
	});
#elif 1
	sharedBufferClient->setNotifyCallback(onNotifyCallbackTest);
#elif 0
	sharedBufferClient->setNotifyCallback(
		std::bind(onNotifyCallbackTest, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
#else
    NotifyCallbackListener *listener = new NotifyCallbackListenerImpl();
    sharedBufferClient->registerCallback(listener);
#endif

	/////////////////// 以下仅为测试代码 ///////////////////////
	while (g_loop) {
		// usleep(40000);
		static int64_t last_read_time_us = 0;
		controlFrameRate(last_read_time_us, (g_cameraFrameRate > 0) ? g_cameraFrameRate : 30);
		// static int mFrameCount;
		// static int mLastFrameCount = 0;
		// static uint64_t mLastFpsTime = 0;
		// static float mFps = 0;
		// debugShowFPS("camera", mFrameCount, mLastFrameCount, mLastFpsTime, mFps);

		if (g_CameraCrash) {
			// 如果收到 camera crash 通知，正确的做法应该是，创建一个线程来重连 sharedBuffer
			sleep(2);	// 等待 camera 进程重新启动
			if (sharedBufferClient != nullptr) {
				ALOGD("reconnect shared buffer ...");
				ret = sharedBufferClient->connect();
				if (ret < 0) {
					ALOGE("connect shared buffer failed");
					continue;
				}
				sharedBufferClient->setNotifyCallback(onNotifyCallbackTest);
				ALOGD("reconnect shared buffer success");
				g_CameraCrash = false;

				uint8_t status = sharedBufferClient->getVirDeviceStatus();
				ALOGD("get vir camera status: %d", status);
				if (status > 0) {
					g_camStatus = (status == VirtualCameraStatus::kOpenBackCamera) ? 
							VirtualCameraStatus::kOpenBackCamera : VirtualCameraStatus::kOpenFrontCamera;
				} else {
					g_camStatus = status;
					continue;
				}
			} else {
				ALOGE("sharedBufferClient is null");
				return -1;
			}
		}

		if (g_camStatus == VirtualCameraStatus::kOpenBackCamera) {

			if (g_cameraFrameSize > maxFrameSize) {
				ALOGE("frame size is too large");
				break;
			}

			if (g_cameraWidth == 1280 && g_cameraHeight == 720) {
				if (feof(backVideoFileHandle_720p)) {
					fseek(backVideoFileHandle_720p, 0, SEEK_SET);
				}
				fread(yuvBuf, g_cameraFrameSize, 1, backVideoFileHandle_720p);
			} else if (g_cameraWidth == 640 && g_cameraHeight == 480) {
				if (feof(videoFileHandle_480p)) {
					fseek(videoFileHandle_480p, 0, SEEK_SET);
				}
				fread(yuvBuf, g_cameraFrameSize, 1, videoFileHandle_480p);
			} else {
				ALOGE("unsupport resolution: %zu x %zu", g_cameraWidth, g_cameraHeight);
				continue;
			}

			// if (feof(backVideoFileHandle_720p)) {
			// 	fseek(backVideoFileHandle_720p, 0, SEEK_SET);
			// }
			// fread(yuvBuf, g_cameraFrameSize, 1, backVideoFileHandle_720p);

			// 注入后置摄像头的图像
			ret = sharedBufferClient->pushFrame(yuvBuf, g_cameraFrameSize, 0);
			if (ret < 0) {
				ALOGE("fill buffer failed");
			}
		} else if (g_camStatus == VirtualCameraStatus::kOpenFrontCamera) {

			if (g_cameraFrameSize > maxFrameSize) {
				ALOGE("frame size is too large");
				break;
			}

			if (g_cameraWidth == 1280 && g_cameraHeight == 720) {
				if (feof(frontVideoFileHandle_720p)) {
					fseek(frontVideoFileHandle_720p, 0, SEEK_SET);
				}
				fread(yuvBuf, g_cameraFrameSize, 1, frontVideoFileHandle_720p);
			} else if (g_cameraWidth == 640 && g_cameraHeight == 480) {
				if (feof(videoFileHandle_480p)) {
					fseek(videoFileHandle_480p, 0, SEEK_SET);
				}
				fread(yuvBuf, g_cameraFrameSize, 1, videoFileHandle_480p);
			} else {
				ALOGE("unsupport resolution: %zu x %zu", g_cameraWidth, g_cameraHeight);
				continue;
			}

			// if (feof(frontVideoFileHandle_720p)) {
			// 	fseek(frontVideoFileHandle_720p, 0, SEEK_SET);
			// }
			// fread(yuvBuf, g_cameraFrameSize, 1, frontVideoFileHandle_720p);

			// 注入前置摄像头的图像
			ret = sharedBufferClient->pushFrame(yuvBuf, g_cameraFrameSize, 0);
			if (ret < 0) {
				ALOGE("fill buffer failed");
			}
		}
	}
	/////////////////////////////////////////////////////////////////

	// sharedBufferClient->unregisterCallback(listener);
	// delete listener;

	sharedBufferClient->disconnect();

	releaseSharedBufferClient(sharedBufferClient);

	if (backVideoFileHandle_720p != nullptr) {
		fclose(backVideoFileHandle_720p);
		backVideoFileHandle_720p = nullptr;
	}

	if (frontVideoFileHandle_720p != nullptr) {
		fclose(frontVideoFileHandle_720p);
		frontVideoFileHandle_720p = nullptr;
	}

	if (videoFileHandle_480p != nullptr) {
		fclose(videoFileHandle_480p);
		videoFileHandle_480p = nullptr;
	}

	if (yuvBuf != nullptr) {
		delete[] yuvBuf;
	}

	return 0;
}

int testMicrophone(const char *fileName)
{
	int ret = 0;
	constexpr size_t frameSize = 960 * 2 * 2;
	uint8_t pcmBbuf[frameSize];

	ALOGD("test Microphone");

 	FILE *fp = fopen(fileName, "rb");
	if (fp == nullptr) {
		printf("open %s failed\n", fileName);
		return -1;
	}

	SharedBufferClientInterface* sharedBufferClient = createSharedBufferClient(VirtualDeviceType::kVirMicrophone);
    if (sharedBufferClient == nullptr) {
		ALOGE("createSharedBufferClient failed");
        return -1;
    }

	ret = sharedBufferClient->connect();
	if (ret < 0) {
		releaseSharedBufferClient(sharedBufferClient);
		ALOGE("connect shared buffer failed");
		return -1;
	}

	// 测试接口 (非必须调用)
	uint8_t status = sharedBufferClient->getVirDeviceStatus();
	ALOGD("get vir mic status: %d", status);

	uint32_t val1 = 0;
	uint32_t val2 = 0;
	uint32_t val3 = 0;
	// 测试接口 (非必须调用)
	ret = sharedBufferClient->getVirDeviceInfo(status, val1, val2, val3);
	if (ret >= 0) {
		ALOGD("get vir mic info: %d, %d, %d, %d", status, val1, val2, val3);
	}

	// 注册回调 (调用一种即可)
#if 0
	sharedBufferClient->setNotifyCallback([&](uint8_t msgType, uint8_t status, int32_t val1, int32_t val2, int32_t val3) {
		ALOGD("msgType=%d, status=%d, val1=%d, val2=%d, val3=%d", msgType, status, val1, val2, val3);
		// 此时，应该通知真实摄像头或真实麦克风，打开设备
		// 比如: 创建一个 Message 再 sendMessage 到其他线程处理。切记：千万不要阻塞回调线程，以防止意想不到的问题。
		// Message m = mEventHandler.obtainMessage(msgType, status, val1, val2, val3);
		// mEventHandler.sendMessage(m);
		// return;
	});
#elif 1
	sharedBufferClient->setNotifyCallback(onNotifyCallbackTest);
#elif 0
	sharedBufferClient->setNotifyCallback(
		std::bind(onNotifyCallbackTest, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
#else
    NotifyCallbackListener *listener = new NotifyCallbackListenerImpl();
    sharedBufferClient->registerCallback(listener);
#endif

	/////////////////// 以下仅为测试代码 ///////////////////////
	while (g_loop) {
		// usleep(20000);
		static int64_t last_read_time_us = 0;
		controlFrameRate(last_read_time_us, (g_micFrameRate > 0) ? g_micFrameRate : 50);
		// static int mFrameCount;
		// static int mLastFrameCount = 0;
		// static uint64_t mLastFpsTime = 0;
		// static float mFps = 0;
		// debugShowFPS("mic", mFrameCount, mLastFrameCount, mLastFpsTime, mFps);

		if (g_MicrophoneCrash) {
			// 如果收到 audio crash 通知，正确的做法应该是，创建一个线程来重连 sharedBuffer
			sleep(2);	// 等待 audio 进程重新启动
			if (sharedBufferClient != nullptr) {
				ALOGD("reconnect shared buffer ...");
				ret = sharedBufferClient->connect();
				if (ret < 0) {
					ALOGE("connect shared buffer failed");
					continue;
				}
				sharedBufferClient->setNotifyCallback(onNotifyCallbackTest);
				ALOGD("reconnect shared buffer success");
				g_MicrophoneCrash = false;

				uint8_t status = sharedBufferClient->getVirDeviceStatus();
				ALOGD("get vir mic status: %d", status);
				g_micStatus = status;
			} else {
				ALOGE("sharedBufferClient is null");
				return -1;
			}
		}

		if (g_micStatus > 0) {
			if (feof(fp)) {
				// break;
				fseek(fp, 0, SEEK_SET);
				continue;
			}
			fread(pcmBbuf, frameSize, 1, fp);

			ret = sharedBufferClient->pushFrame(pcmBbuf, frameSize, 0);
			if (ret < 0) {
				ALOGE("fill buffer failed");
			}
		}
	}
	//////////////////////////////////////////////////////////////

	// sharedBufferClient->unregisterCallback(listener);
	// delete listener;

	sharedBufferClient->disconnect();

	releaseSharedBufferClient(sharedBufferClient);

	if (fp != nullptr) {
		fclose(fp);
		fp = nullptr;
	}

	return 0;
}


bool test_dlopen()
{
	ALOGD("test_dlopen");

	typedef int (*getCameraConfigInt_t)(const char* type);
	typedef const char* (*getCameraConfigStr_t)(const char* type);
	getCameraConfigInt_t getTestInt = nullptr;
	getCameraConfigStr_t getTestStr = nullptr;
	getCameraConfigInt_t getCameraConfigInt = nullptr;
	getCameraConfigStr_t getCameraConfigStr = nullptr;

	const char* lib_path = "/system/lib64/libsharedbufferclient.sbc.so";
	void* handle = dlopen(lib_path, RTLD_LAZY);
	if (!handle) {
		ALOGE("dlopen %s failed", lib_path);
		return false;
	}

	if ((getTestInt = (getCameraConfigInt_t) dlsym(handle, "getTestInt")) != nullptr) {
		int ret = getTestInt("cameraPixFmt");
		ALOGD("getTestInt ret: %d", ret);
	} else {
		ALOGE("getTestInt is null");
	}

	if ((getTestStr = (getCameraConfigStr_t) dlsym(handle, "getTestStr")) != nullptr) {
		const char *ret = getTestStr("cameraPixFmt");
		ALOGD("getTestInt ret: %s", ret);
	} else {
		ALOGE("getTestStr is null");
	}

	if ((getCameraConfigInt = (getCameraConfigInt_t) dlsym(handle, "getCameraConfigInt")) != nullptr) {
		int ret = getCameraConfigInt("cameraPixFmt");
		ALOGD("getCameraConfigInt ret: %d", ret);
	} else {
		ALOGE("getCameraConfigInt is null");
	}

	if ((getCameraConfigStr = (getCameraConfigStr_t) dlsym(handle, "getCameraConfigStr")) != nullptr) {
		const char *ret = getCameraConfigStr("cameraPixFmt");
		ALOGD("getCameraConfigStr ret: %s", ret);
	} else {
		ALOGE("getCameraConfigStr is null");
	}

	if (handle != nullptr) {
		dlclose(handle);
	}

  	return true;
}

int main(int argc, char **argv) {

	if (argc != 5) {
		// printf("usage: %s <back_video.yuv> <front_video.yuv> <audio.pcm>\n", argv[0]);
		printf("usage: %s <720p_back_vide0.yuv> 720p_front_video.yuv> <480p_video.yuv> <audio.pcm>\n", argv[0]);
		return -1;
	}

	test_dlopen();
	//ALOGD("getCameraConfigInt ret: %d", getCameraConfigInt("cameraPixFmt"));
	//ALOGD("getCameraConfigStr ret: %s", getCameraConfigStr("cameraPixFmt"));

	std::thread testCameraThread([&]() {
		int ret = testCamera(argv[1], argv[2], argv[3]);
		if (ret < 0) {
			g_loop = false;
			ALOGE("testCamera failed");
		}
		ALOGD("exit test mic thread");
	});

	std::thread testMicrophoneThread([&]() {
		int ret = testMicrophone(argv[4]);
		if (ret < 0) {
			g_loop = false;
			ALOGE("testMicrophone failed");
		}
		ALOGD("exit test mic thread");
	});

	printf("double enter to exit: \n");
	getchar();
	getchar();

	g_loop = false;

	testMicrophoneThread.join();
	testCameraThread.join();

    return 0;
}
