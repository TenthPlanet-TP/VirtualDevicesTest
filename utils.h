#ifndef __UTILS_H__
#define __UTILS_H__

#include <android/log.h>

#ifdef LOG_TAG
    #undef LOG_TAG
#endif

#define LOG_TAG     "NdkMediaExtractorTest"

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

class Utils
{
public:
    Utils(/* args */) = default;
    ~Utils() = default;

    static int64_t getCurrentTimeUs();

    static int64_t getCurrentTimeMs();

    static void debugShowFPS(const char *tag, int &mFrameCount, int &mLastFrameCount, uint64_t &mLastFpsTime, float mFps);

    static void controlFrameRate(int64_t &last_read_time_us, int framerate);
};


#endif
