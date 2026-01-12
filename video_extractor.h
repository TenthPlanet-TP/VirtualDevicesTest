
#pragma once

#ifndef __VIDEO_EXTRACTOR_H__
#define __VIDEO_EXTRACTOR_H__

#include <iostream>
#include <queue>
#include <list>
#include <deque>
#include <thread>
#include <iostream>
#include <string>
#include <mutex>
#include <atomic>
#include <map>
#include <atomic>

#if 0
#include <ndk/NdkMediaCodec.h>
#include <ndk/NdkMediaFormat.h>
#include <ndk/NdkMediaExtractor.h>
#elif 1
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaExtractor.h>

#endif



// struct CodecState {
//     sp<MediaCodec> mCodec;
//     Vector<sp<MediaCodecBuffer> > mInBuffers;
//     Vector<sp<MediaCodecBuffer> > mOutBuffers;
//     bool mSignalledInputEOS;
//     bool mSawOutputEOS;
//     int64_t mNumBuffersDecoded;
//     int64_t mNumBytesDecoded;
//     bool mIsAudio;
// };

struct YuvFrame {
    YuvFrame(size_t size, int64_t pts = -1, int32_t fps = 30, int32_t width = 0, int32_t height = 0)
        : mBuf(new unsigned char[size])
        , mSize(size)
        , mPts(pts)
        , mFps(fps)
        , mWidth(width)
        , mHeight(height)
    {
        // LOG_I("YuvFrame ctor called");
    }
    
    ~YuvFrame() {
        //  LOG_I("dtor YuvFrame called");
        if (mBuf != nullptr) {
            delete[] mBuf;
            mBuf = nullptr;
        }
    }

    unsigned char *mBuf;
    size_t mSize;
    int64_t mPts;
    int32_t mFps;
    int32_t mWidth;
    int32_t mHeight;
};

class MyVideoExtractor {
public:
    using FrameCallback = std::function<void(const uint8_t *frameBuf, size_t frameSize, int64_t pts)>;

	MyVideoExtractor(void);
	~MyVideoExtractor(void);

    int create(const char *fileName, bool continuous = false);
    
    int release(void);

    void setFrameCallback(const FrameCallback& callback) {
        mFrameCallback = callback;
    }

    int sendYuvFrame(const std::shared_ptr<YuvFrame>& frame);

    int fastForward(int64_t duration);

    int pause(bool pause);
    int setContinuous(bool continuous);
    bool isPlaying(void);
    void play(bool play);

    int64_t getDurationUs(void) { return mDurationUs; }

    int createVideoExtractorThread(int64_t startUs = 0);

    int getWidth() { return mWidth; }
    int getHeight() { return mHeight; }
    int getFps() { return mFrameRateFps; }

private:
    int videoExtractorThread(void);
    void releaseInner(void);

    int input(void);
    int decode(void);

private:
	AMediaExtractor *mVideoExtractor = nullptr;
	AMediaFormat *mVideoFmt = nullptr;
    // AMediaCodec* mDecoder = nullptr;
    AMediaCodec* mVideoDecoder = nullptr;

	int mVideoTrack = -1;

    int32_t mMaxInputSize = -1;
    std::vector<uint8_t> mSampleBuf;
    size_t mSampleBufSize;

    int mWidth = 0;
    int mHeight = 0;
    int mWidthStride = 0;

    int mOriFrameSize;
    int mFrameSize;
    int mOriHeight;

    int32_t mFrameRateFps = -1;
    int64_t mDurationUs = -1;
    int32_t mRotationDegrees;
    int32_t mColorFmt;

    static const int kMicroSecondsPerSecond = 1000 * 1000;
    static const int kMillisecondsPerSecond = 1000;

    std::unique_ptr<std::thread> mVideoExtractorThread;
    // std::thread *mVideoExtractorThread;
    // bool mRunning;
    enum class ThreadState { UNINITIALIZED, INIT, RUNNING, WAITING, STOPPING, STOPPED } mThreadState;

    bool mIsPause = false;
    bool mIsPlaying = false;
    bool mPlay = false;
    
    std::atomic<bool> mContinuous;
    
    // bool mGetFrame;
    std::mutex mMutex;
    std::condition_variable mCondition;

    std::atomic<uint16_t> mCacheCnt;
    bool mInputEof;
    bool mOutputEof;
	bool mInited;

    int mOldCameraWidth = 0;
    int mOldCameraHeight = 0;
    int mOldCameraFps = 0;

    int mOldCameraBackOrientation = 0;
    int mOldCameraFrontOrientation = 0;

    int64_t mStartTimeUs;
    int64_t mSleepTimeUs;

    int mCacheFrameCnt;
    bool mCache;
    int64_t mIsFirstFramePts;

    FrameCallback mFrameCallback;
};


#endif
