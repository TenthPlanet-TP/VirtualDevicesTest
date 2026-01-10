// #pragma once
#ifndef __SHAREDBUFFERCLIENTINTERFACE_H__
#define __SHAREDBUFFERCLIENTINTERFACE_H__


#include <iostream>
#include <functional>

class NotifyCallbackListener {
public:
	virtual ~NotifyCallbackListener() {}

	/**
	 * 通知回调
	 * 参数：
	 * 		msgType: VirtualDeviceType
	 * 
	 * 		如果 msgType == VirtualDeviceType::kVirCamera 				那么 status: VirtualCameraStatus, val1: width, val2: height, val3: fps
	 * 		如果 msgType == VirtualDeviceType::kVirMicrophone 			那么 status: VirtualMicrophoneStatus, val1: sampleRate, val2: channel, val3: period_size
	 * 		如果 msgType == VirtualDeviceType::kVirFlashLight 			那么 status: kVirFlashLightMode, val1: reserve, val2: reserve, val3: reserve
	 * 		如果 msgType == VirtualDeviceType::kAeExposureCompensation 	那么 status: 曝光补偿值, val1: 18, val2: 167, val3: reserve
	 * 		如果 msgType == VirtualDeviceType::kVirDevicesCrash 		那么 status: kVirCamera/kVirMicrophone
	*/
	virtual void onNotifyCallback(uint8_t msgType, uint8_t status, int32_t val1, int32_t val2, int32_t val3) = 0;
};

// 1. 摄像头
enum /* class */ VirtualCameraStatus : uint8_t {
	kCloseCamera = 0,
	kOpenBackCamera = 1,
	kOpenFrontCamera = 2,
};

// 2. 麦克风
enum /* class */ VirtualMicrophoneStatus : uint8_t {
	kClose = 0,
	kOpen = 1,
};

// 3. 闪光灯
enum /* class */ kVirFlashLightMode : uint8_t {
	kNone,		/* 关闭闪光灯 */
	kFlash,		/* 拍照时闪光灯会闪烁一次 */
	kTorch,		/* 手电筒模式，闪光灯会持续亮起 */
};

// 4. 曝光补偿
/**
 * 曝光补偿参数说明：
 * status : 曝光补偿值。因为曝光值有可能是一个负数，为了方便网络传输，所以在 SDK 内部把真正的曝光补偿值加上 18，把它变成一个正数，因此为了获取真正的曝光补偿值需要客户手动减去 18
 * val1	  : 固定为 18，表示曝光补偿值范围的绝对值 [-18, 18]。有些手机的曝光补偿值范围可能是 [-12, 12]，因此需要客户做下算数转换。例如：(曝光补偿值 - 18) / 18 * 12
 * val2	  : 固定为 167，表示曝光补偿的步长值。实际真正的曝光补偿的步长值为 1/6, 是一个小数，所以这里把步长值 * 1000，变成 167，因此为了获取真正的曝光补偿值需要客户手动除以 1000
*/


enum /* class */ VirtualDeviceType : uint8_t {
	kVirCamera 				= 0,
	kVirMicrophone 			= 1,
	kVirAudioOut    		= 2,	/* reserve */
	kVirFlashLight  		= 3,	/* 闪光灯 */	/* 注意：Android 12/13/14 已支持，Android 10 暂未实现  */
	kAeExposureCompensation	= 4,	/* 曝光补偿 */
	kVirDevicesCrash 		= 255	/* camera or audio may crash */
};

typedef struct FrameConfig {
    VirtualDeviceType type;
	union {
		struct {
			int width;
			int height;
			int format;
			int rotation;
			int cameraId;
			int64_t pts;	/* ms */
		} VideoFrameConfig;
		struct {
			int channel;
			int sampleRate;
			int64_t pts;	/* ms */
		} AudioFrameConfig;
	} InnerUnion;
} T_FrameConfig, *PT_FrameConfig;


class SharedBufferClientInterface {
public:
	/**
	 * 通知回调
	 * 参数：
	 * 		msgType: VirtualDeviceType
	 * 
	 * 		如果 msgType == VirtualDeviceType::kVirCamera 				那么 status: VirtualCameraStatus, val1: width, val2: height, val3: fps
	 * 		如果 msgType == VirtualDeviceType::kVirMicrophone 			那么 status: VirtualMicrophoneStatus, val1: sampleRate, val2: channel, val3: period_size
	 * 		如果 msgType == VirtualDeviceType::kVirFlashLight 			那么 status: kVirFlashLightMode, val1: reserve, val2: reserve, val3: reserve
	 * 		如果 msgType == VirtualDeviceType::kAeExposureCompensation 	那么 status: 曝光补偿值, val1: 18, val2: 167, val3: reserve
	 * 		如果 msgType == VirtualDeviceType::kVirDevicesCrash 		那么 status: kVirCamera/kVirMicrophone
	*/
	using NotifyCallback = std::function<void(uint8_t msgType, uint8_t status, int32_t val1, int32_t val2, int32_t val3)>;

	SharedBufferClientInterface() = default;
	virtual ~SharedBufferClientInterface() = default;

	/**
	 * 连接共享内存
	 * 返回值：
	 * 		0  - 成功
	 * 		-1 - 失败
	*/
	virtual int connect(void) = 0;

	/**
	 * 断开共享内存
	*/
	virtual void disconnect(void) = 0;
	
	/**
	 * reserve
	*/
    virtual void checkBufferStatus(void) = 0;

	/**
	 * 注入 YUV / PCM
	 * 返回值：
	 * 		0  - 成功
	 * 		-1 - 失败
	*/
	virtual int pushFrame(const uint8_t *buf, const size_t size, int64_t pts = -1 /* ms */) = 0;
	
	virtual int pushFrame(const uint8_t *buf, const size_t size, const FrameConfig &config) = 0;

	/**
	 * 获取虚拟摄像头/麦克风的状态
	 * 返回值：
	 * 		虚拟摄像头/虚拟麦克风的状态
	*/
	virtual uint8_t getVirDeviceStatus(void) = 0;

	/**
	 * 获取虚拟摄像头状态、分辨率和帧率的信息/麦克风的状态、采样率、通道数和音频帧周期大小的信息
	 * 返回值：
	 * 		0  - 成功
	 * 		-1 - 失败
	*/
	virtual int getVirDeviceInfo(uint8_t& status, uint32_t& val1, uint32_t& val2, uint32_t& val3) = 0;

	/**
	 * 注册通知回调，当打开摄像头或麦克风时，会调用该接口
	 * 返回值：
	 * 		0  - 成功
	 * 		-1 - 失败
	*/
  	virtual int registerCallback(NotifyCallbackListener* listener) = 0;

	/**
	 * 取消注册通知回调
	 * 返回值：
	 * 		0  - 成功
	 * 		-1 - 失败
	*/
  	virtual int unregisterCallback(NotifyCallbackListener* listener) = 0;

	/**
	 * 设置通知回调，当打开摄像头或麦克风时，会调用该接口
	*/
    virtual void setNotifyCallback(const NotifyCallback& cb) = 0;

    virtual void setNotifyCallback(NotifyCallback&& cb) = 0;
};

/**
 * 创建共享内存客户端
 * 参数：
 * 		kVirCamera 		- 表示创建虚拟摄像头的共享内存客户端
 * 		kVirMicrophone	- 表示创建虚拟麦克风的共享内存客户端
*/
SharedBufferClientInterface* createSharedBufferClient(VirtualDeviceType type);

/**
 * 释放共享内存客户端
 * 参数：
 * 		createSharedBufferClient 返回的指针
*/
void releaseSharedBufferClient(SharedBufferClientInterface* interface);


#ifdef __cplusplus
extern "C"{
#endif

/**
 * 获取 Camera 的一些配置
 * 参数："cameraPixFmt"
 * 返回值：
 * 		如果是 Android 10，则返回 24
 * 		如果是 Android 12+，则返回 23
*/
int getCameraConfigInt(const char* type);

/**
 * 获取 Camera 的一些配置
 * 参数："cameraPixFmt"
 * 返回值：
 * 		如果是 Android 10，则返回 "nv21"
 * 		如果是 Android 12+，则返回 "nv12"
*/
const char* getCameraConfigStr(const char* type);

#ifdef __cplusplus
}
#endif


#endif