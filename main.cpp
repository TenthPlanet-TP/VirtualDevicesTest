#include "SharedBufferClientInterface.h"

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include <string>
#include <thread>
#include <dlfcn.h>
#include "utils.h"
#include "video_extractor.h"

// #define LOG_TAG "VirDevTestDemo"


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


int testCamera(const char *fileName)
{
	int ret = 0;

	ALOGD("test camera");

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

    NotifyCallbackListener *listener = new NotifyCallbackListenerImpl();
    sharedBufferClient->registerCallback(listener);

    std::unique_ptr<MyVideoExtractor> videoExtractor = std::make_unique<MyVideoExtractor>();
    if (videoExtractor == nullptr) {
        LOG_E("new mVideoExtractor failed");
    } else {
        videoExtractor->setFrameCallback([&](const uint8_t* yuvBuf, size_t size, int64_t pts) {
            // 处理每一帧视频数据
            if (g_camStatus == VirtualCameraStatus::kOpenFrontCamera) {

                if (g_cameraFrameSize != size) {
                    ALOGE("frame size is too large");
                    return;
                }
    
                // 注入前置摄像头的图像
                ret = sharedBufferClient->pushFrame(yuvBuf, g_cameraFrameSize, 0);
                if (ret < 0) {
                    ALOGE("fill buffer failed");
                }
            }
        });

        ret = videoExtractor->create(fileName, true);
    }

	while (g_loop) {
		sleep(1);   // sleep 1s
	}
	/////////////////////////////////////////////////////////////////

    if (videoExtractor != nullptr) {
        videoExtractor->release();
    }

    if (listener != nullptr) {
        sharedBufferClient->unregisterCallback(listener);
        delete listener;
    }

	sharedBufferClient->disconnect();

	releaseSharedBufferClient(sharedBufferClient);

	return 0;
}

// int testMicrophone(const char *fileName)
// {
// 	int ret = 0;
// 	constexpr size_t frameSize = 960 * 2 * 2;
// 	uint8_t pcmBbuf[frameSize];

// 	ALOGD("test Microphone");

//  	FILE *fp = fopen(fileName, "rb");
// 	if (fp == nullptr) {
// 		printf("open %s failed\n", fileName);
// 		return -1;
// 	}

// 	SharedBufferClientInterface* sharedBufferClient = createSharedBufferClient(VirtualDeviceType::kVirMicrophone);
//     if (sharedBufferClient == nullptr) {
// 		ALOGE("createSharedBufferClient failed");
//         return -1;
//     }

// 	ret = sharedBufferClient->connect();
// 	if (ret < 0) {
// 		releaseSharedBufferClient(sharedBufferClient);
// 		ALOGE("connect shared buffer failed");
// 		return -1;
// 	}

// 	// 测试接口 (非必须调用)
// 	uint8_t status = sharedBufferClient->getVirDeviceStatus();
// 	ALOGD("get vir mic status: %d", status);

// 	uint32_t val1 = 0;
// 	uint32_t val2 = 0;
// 	uint32_t val3 = 0;
// 	// 测试接口 (非必须调用)
// 	ret = sharedBufferClient->getVirDeviceInfo(status, val1, val2, val3);
// 	if (ret >= 0) {
// 		ALOGD("get vir mic info: %d, %d, %d, %d", status, val1, val2, val3);
// 	}

// 	// 注册回调 (调用一种即可)
// #if 0
// 	sharedBufferClient->setNotifyCallback([&](uint8_t msgType, uint8_t status, int32_t val1, int32_t val2, int32_t val3) {
// 		ALOGD("msgType=%d, status=%d, val1=%d, val2=%d, val3=%d", msgType, status, val1, val2, val3);
// 		// 此时，应该通知真实摄像头或真实麦克风，打开设备
// 		// 比如: 创建一个 Message 再 sendMessage 到其他线程处理。切记：千万不要阻塞回调线程，以防止意想不到的问题。
// 		// Message m = mEventHandler.obtainMessage(msgType, status, val1, val2, val3);
// 		// mEventHandler.sendMessage(m);
// 		// return;
// 	});
// #elif 1
// 	sharedBufferClient->setNotifyCallback(onNotifyCallbackTest);
// #elif 0
// 	sharedBufferClient->setNotifyCallback(
// 		std::bind(onNotifyCallbackTest, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
// #else
//     NotifyCallbackListener *listener = new NotifyCallbackListenerImpl();
//     sharedBufferClient->registerCallback(listener);
// #endif

// 	/////////////////// 以下仅为测试代码 ///////////////////////
// 	while (g_loop) {
// 		// usleep(20000);
// 		static int64_t last_read_time_us = 0;
// 		controlFrameRate(last_read_time_us, (g_micFrameRate > 0) ? g_micFrameRate : 50);
// 		// static int mFrameCount;
// 		// static int mLastFrameCount = 0;
// 		// static uint64_t mLastFpsTime = 0;
// 		// static float mFps = 0;
// 		// debugShowFPS("mic", mFrameCount, mLastFrameCount, mLastFpsTime, mFps);

// 		if (g_MicrophoneCrash) {
// 			// 如果收到 audio crash 通知，正确的做法应该是，创建一个线程来重连 sharedBuffer
// 			sleep(2);	// 等待 audio 进程重新启动
// 			if (sharedBufferClient != nullptr) {
// 				ALOGD("reconnect shared buffer ...");
// 				ret = sharedBufferClient->connect();
// 				if (ret < 0) {
// 					ALOGE("connect shared buffer failed");
// 					continue;
// 				}
// 				sharedBufferClient->setNotifyCallback(onNotifyCallbackTest);
// 				ALOGD("reconnect shared buffer success");
// 				g_MicrophoneCrash = false;

// 				uint8_t status = sharedBufferClient->getVirDeviceStatus();
// 				ALOGD("get vir mic status: %d", status);
// 				g_micStatus = status;
// 			} else {
// 				ALOGE("sharedBufferClient is null");
// 				return -1;
// 			}
// 		}

// 		if (g_micStatus > 0) {
// 			if (feof(fp)) {
// 				// break;
// 				fseek(fp, 0, SEEK_SET);
// 				continue;
// 			}
// 			fread(pcmBbuf, frameSize, 1, fp);

// 			ret = sharedBufferClient->pushFrame(pcmBbuf, frameSize, 0);
// 			if (ret < 0) {
// 				ALOGE("fill buffer failed");
// 			}
// 		}
// 	}
// 	//////////////////////////////////////////////////////////////

// 	// sharedBufferClient->unregisterCallback(listener);
// 	// delete listener;

// 	sharedBufferClient->disconnect();

// 	releaseSharedBufferClient(sharedBufferClient);

// 	if (fp != nullptr) {
// 		fclose(fp);
// 		fp = nullptr;
// 	}

// 	return 0;
// }



int main(int argc, char **argv) {
	if (argc != 2) {
		printf("usage: %s <video.mp4>\n", argv[0]);
		return -1;
	}

	std::thread testCameraThread([&]() {
		int ret = testCamera(argv[1]);
		if (ret < 0) {
			g_loop = false;
			ALOGE("testCamera failed");
		}
		ALOGD("exit test cam thread");
	});

	// std::thread testMicrophoneThread([&]() {
	// 	int ret = testMicrophone(argv[4]);
	// 	if (ret < 0) {
	// 		g_loop = false;
	// 		ALOGE("testMicrophone failed");
	// 	}
	// 	ALOGD("exit test mic thread");
	// });

	printf("double enter to exit: \n");
	getchar();
	getchar();

	g_loop = false;

	// testMicrophoneThread.join();
	testCameraThread.join();

    return 0;
}
