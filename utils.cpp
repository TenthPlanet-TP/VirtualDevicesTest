#include "utils.h"

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

int64_t Utils::getCurrentTimeUs()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int64_t now_us = ((uint64_t)tv.tv_sec) * 1000000 + ((uint64_t)tv.tv_usec);
	return now_us;
}

int64_t Utils::getCurrentTimeMs() {
	return getCurrentTimeUs() / 1000;
}

void Utils::debugShowFPS(const char *tag, int &mFrameCount, int &mLastFrameCount, uint64_t &mLastFpsTime, float mFps)
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
void Utils::controlFrameRate(int64_t &last_read_time_us, int framerate)
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

