
#pragma once

#ifndef __AUDIO_EXTRACTOR_H__
#define __AUDIO_EXTRACTOR_H__


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

struct PcmFrame {
    PcmFrame(size_t size, int64_t pts = -1, bool cache = true)
        : mBuf(new unsigned char[size])
        , mSize(size)
        , mPts(pts)
        , mCachable(cache)
    {
        // ALOGD("PcmFrame ctor called");
    }

    ~PcmFrame() {
        //  ALOGD("dtor YuvFrame called");
        if (mBuf != nullptr) {
            delete[] mBuf;
            mBuf = nullptr;
        }
    }

    unsigned char *mBuf;
    size_t mSize;
    int64_t mPts;
    bool mCachable;
};

class MyAudioExtractor {
public:
    using FrameCallback = std::function<void(const uint8_t *frameBuf, size_t frameSize, int64_t pts)>;

	MyAudioExtractor();
	~MyAudioExtractor(void);

    int create(const char *fileName, bool continuous = false);
    
    int release(void);

    void setFrameCallback(const FrameCallback& callback) {
        mFrameCallback = callback;
    }

    int sendPcmFrame(const std::shared_ptr<PcmFrame>& frame);

    int fastForward(int64_t duration);

    int pause(bool pause);
    void play(bool play);
    int setContinuous(bool continuous);
    bool isPlaying(void);

    int64_t getDurationUs(void) { return mDurationUs; }

    int createAudioExtractorThread(int64_t startUs = 0);

private:

    int input(void);

    int decode(void);
    
    int audioExtractorThread(void);
    void releaseInner(void);

private:
	AMediaExtractor *mExtractor = nullptr;
    AMediaFormat *mAudioFmt = nullptr;
    // AMediaCodec* mDecoder = nullptr;
    AMediaCodec* mAudioDecoder = nullptr;

	int mAudioTrack = -1;

	int32_t mSampleRate = -1;
	int32_t mChannelCount = -1;
	int32_t mAacProfile = -1;

    int32_t mMaxInputSize = -1;
    std::vector<uint8_t> mSampleBuf;
    size_t mSampleBufSize;

    int64_t mDurationUs = -1;

    int16_t mPeriodSize = 0;

    std::deque<int64_t> mTimestampQueue;
    static const int kMicroSecondsPerSecond = 1000 * 1000;
    static const int kMillisecondsPerSecond = 1000;

    // const int k44100HzFrameSize = 4096;

    std::unique_ptr<std::thread> mAudioExtractorThread;
    // bool mRunning;
    enum class ThreadState { UNINITIALIZED, INIT, RUNNING, STOPPING, STOPPED } mThreadState;

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

    int64_t mStartTimeUs;
    int64_t mSleepTimeUs;

    // FILE *m_fp = nullptr;
    int mCacheFrameCnt;
    bool mCache;
    int64_t mIsFirstFramePts;

    FrameCallback mFrameCallback;
};


#endif
