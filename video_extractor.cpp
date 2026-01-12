
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <sched.h>
#include <pthread.h>

#include <string>
#include <thread>
#include <vector>

#include "video_extractor.h"
#include "utils.h"

// #include "json_protocol.h"


// #ifdef MODULE_NAME
// #undef MODULE_NAME
// #endif
// #define MODULE_NAME "VideoExtractor"

#undef MIN
#define MIN(_a,_b) ((_a) > (_b) ? (_b) : (_a))

// #define TIMEOUT_US      5000
// #define TIMEOUT_US      20000
#define TIMEOUT_US      10000

// WebRTC queues input frames quickly in the beginning on the call. Wait for input buffers with a
// long timeout (500 ms) to prevent this from causing the codec to return an error.
#define DEQUEUE_INPUT_TIMEOUT_US            500000
// #define DEQUEUE_INPUT_TIMEOUT_US            50000

// Dequeuing an output buffer will block until a buffer is available (up to 100 milliseconds).
// If this timeout is exceeded, the output thread will unblock and check if the decoder is still
// running.  If it is, it will block on dequeue again.  Otherwise, it will stop and release the
// MediaCodec.
#define DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US    100000
// #define DEQUEUE_OUTPUT_BUFFER_TIMEOUT_1_US    20000
// #define DEQUEUE_OUTPUT_BUFFER_TIMEOUT_2_US    10000


#define OUTPUT_BUFFERS_CHANGED              100
#define OUTPUT_FORMAT_CHANGED               101



// #define MAX_WIDTH         1280
// #define MAX_HEIGHT        720
#define MAX_WIDTH         1920
#define MAX_HEIGHT        1080
#define MAX_VIDEO_FPS       30


#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))


// using namespace android;


MyVideoExtractor::MyVideoExtractor(void)
    : mThreadState(ThreadState::UNINITIALIZED)
    , mIsPause(false)
    , mContinuous(false)
    , mInputEof(false)
    , mOutputEof(false)
    , mInited(false)
    , mCache(false)
    , mIsFirstFramePts(-1)
{

}

MyVideoExtractor::~MyVideoExtractor() {
    release();
}

int MyVideoExtractor::create(const char *fileName, bool continuous)
{
    LOG_I(".");
	bool useVideo = true;
    bool haveVideo = false; 
	bool isVideo = false;
	
    std::string videoMime;
    // const char *mime;

    if (fileName == nullptr) {
        LOG_E("fileName is null");
        return -1;
    }

    // 获取文件后缀，过滤一些不支持的格式
    // ...

    int fd = open(fileName, O_RDONLY | O_LARGEFILE);
    if (fd < 0) {
        LOG_E("open %s failed\n", fileName);
        return -1;
    }

#if 0
    struct stat buf;
    if (!stat(fileName, &buf)) {
        printf("size of file : %ld\n", buf.st_size);
    } else {
        printf("stat failed\n");
        return -1;
    }
#else
    off64_t fileSize = lseek64(fd, 0, SEEK_END);
    if (fileSize <= 0ll) {
        printf("lseek64 failed\n");
        close(fd);
        return -1;
    }
    // printf("size of file : %ld\n", fileSize);
#endif

    release();

    std::lock_guard<std::mutex> lock(mMutex);

    mVideoExtractor = AMediaExtractor_new();
    if (mVideoExtractor == nullptr) {
        LOG_E("AMediaExtractor_new error.\n");
        close(fd);
        return -1;
    }

    // media_status_t ret = AMediaExtractor_setDataSourceFd(mVideoExtractor, fd, 0, buf.st_size);
    media_status_t ret = AMediaExtractor_setDataSourceFd(mVideoExtractor, fd, 0, fileSize);
    if (AMEDIA_OK != ret) {
        LOG_E("AMediaExtractor_setDataSourceFd failed.\n");
        AMediaExtractor_delete(mVideoExtractor);
        close(fd);
        return -1;
    }

    if (fd > 0) {
        close(fd);
        fd = -1;
    }

    size_t trackCount = AMediaExtractor_getTrackCount(mVideoExtractor);
    if (trackCount <= 0) {
        LOG_E("AMediaExtractor_getTrackCount failed.\n");
        AMediaExtractor_delete(mVideoExtractor);
        return -1;
    }

    for (size_t i = 0; i < trackCount; ++i) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(mVideoExtractor, i);
        if (format == nullptr) {
            AMediaExtractor_delete(mVideoExtractor);
            LOG_E("AMediaExtractor_getTrackFormat failed.\n");
            return -1;
        }

        const char *mime;
        if (!AMediaFormat_getString(format, "mime", &mime)) {
            continue;
        } else if (!strncasecmp(mime, "video/", 6)) {
            mVideoFmt = format;
            AMediaExtractor_selectTrack(mVideoExtractor, i);
			LOG_I("mime: %s, video track is : %zu\n", mime, i);
			mVideoTrack = i;
			isVideo = true;

            videoMime = mime;
            videoMime.assign(mime);
        } 
		if (isVideo) {
			break;
		}
    }
    
    if (useVideo && !haveVideo && isVideo) {
		haveVideo = true;
        
        mWidth = 0;
        mHeight = 0;
        mWidthStride = 0;
        mMaxInputSize = 0;
        mFrameRateFps = 0;
        mDurationUs = 0;
        mRotationDegrees = 0;

        bool ret = false;
		AMediaFormat_getInt32(mVideoFmt, "width", &mWidth);
		AMediaFormat_getInt32(mVideoFmt, "height", &mHeight);
        AMediaFormat_getInt32(mVideoFmt, "stride", &mWidthStride);
		AMediaFormat_getInt32(mVideoFmt, "max-input-size", &mMaxInputSize); // 得到能获取的有关视频的最大值
		AMediaFormat_getInt32(mVideoFmt, "frame-rate", &mFrameRateFps);
		AMediaFormat_getInt32(mVideoFmt, "rotation-degrees", &mRotationDegrees);
		AMediaFormat_getInt64(mVideoFmt, "durationUs", &mDurationUs);

        if (mWidthStride != mWidth) {
            LOG_I("stride(%d) != width(%d)", mWidthStride, mWidth);
        }

        LOG_I("create video format: mWidth=%d, mHeight=%d, mWidthStride=%d, durationUs=%ld, mMaxInputSize=%d, degrees=%d", 
                mWidth, mHeight, mWidthStride, mDurationUs, mMaxInputSize, mRotationDegrees);

#if 1
        if (mWidth < 0 || mWidth > MAX_WIDTH || mHeight < 0 || mHeight > MAX_HEIGHT) {
            LOG_E("set camera width/heigth failed (%dx%d), must be small than 1920x1080", mWidth, mHeight);
            AMediaFormat_delete(mVideoFmt);
            AMediaExtractor_delete(mVideoExtractor);
            return -1;
        }
#else
        if (mWidth != FIXED_WIDTH || mHeight != FIXED_HEIGHT || (mFrameRateFps > MAX_VIDEO_FPS)) {
            LOG_E("set camera width/heigth failed (%dx%d@%dfps), must be 1920x1080@30fps", mWidth, mHeight, mFrameRateFps);
            AMediaFormat_delete(mVideoFmt);
            AMediaExtractor_delete(mVideoExtractor);
            return -2;
        }
#endif

        mOriFrameSize = mWidth * mHeight;
        mFrameSize = mOriFrameSize;
        mOriHeight = mHeight;

        if (mMaxInputSize > 0) {
            mSampleBuf.reserve(mMaxInputSize);
            mSampleBufSize = mMaxInputSize;
        }

#if 1
        // LOG_I("videoMime: %s", videoMime.c_str());
        mVideoDecoder = AMediaCodec_createDecoderByType(videoMime.c_str());
        if (nullptr == mVideoDecoder) {
            LOG_E("AMediaCodec_createDecoderByType failed\n");
            AMediaFormat_delete(mVideoFmt);
            AMediaExtractor_delete(mVideoExtractor);
            return -1;
        }
#else
        mVideoDecoder = AMediaCodec_createCodecByName("OMX.google.h264.decoder");
        if (nullptr == mVideoDecoder) {
            LOG_E("AMediaCodec_createDecoderByType failed\n");
            AMediaFormat_delete(mVideoFmt);
            AMediaExtractor_delete(mVideoExtractor);
            return -1;
        }
#endif

        media_status_t status = AMediaCodec_configure(mVideoDecoder, mVideoFmt, NULL, NULL, 0);
        if (status != AMEDIA_OK) {
            LOG_E("AMediaCodec_configure error\n");
            AMediaFormat_delete(mVideoFmt);
            AMediaCodec_delete(mVideoDecoder);
            mVideoDecoder = NULL;
            return -1;
        }

        status = AMediaCodec_start(mVideoDecoder);
        if (status != AMEDIA_OK) {
            LOG_E("AMediaCodec_start error\n");
            AMediaFormat_delete(mVideoFmt);
            AMediaCodec_delete(mVideoDecoder);
            mVideoDecoder = NULL;
            return -1;
        }

        // char cam_config[48];
        // snprintf(cam_config, ARRAY_SIZE(cam_config), "%dx%d@%d-live", mWidth, mHeight, mFrameRateFps);
        // LOG_I("set camera config: %s", cam_config);

        // property_set("vir.camera.config", cam_config);
        // // property_set("vir.camera.live", "true");

        // if (mWidth > mHeight) {
        //     property_set("vir.camera.orien", "90");
        //     LOG_I("set camera orientation : 90");
        // } else {
        //     property_set("vir.camera.orien", "0");
        //     LOG_I("set camera orientation, 0");
        // }
    
	} else {
        LOG_E("can not find video track\n");
        AMediaFormat_delete(mVideoFmt);
        AMediaCodec_delete(mVideoDecoder);
        return -1;
    }

    // https://github.com/huazi5D/MediaExtractorTest/blob/master/app/src/main/java/hz/mediaextractortest/MediaExtractorManager.java
    // //重新切换此信道，不然上面跳过了3帧，造成前面的帧数模糊
    // mMediaExtractor.unselectTrack(videoTrackIndex);

    // AMediaExtractor_unselectTrack(mVideoExtractor, mVideoTrack);
    // AMediaExtractor_selectTrack(mVideoExtractor, mAudioTrack);

    mCacheFrameCnt = 0;

    mContinuous = continuous;
    mInited = true;
    mCache = true;
    mIsFirstFramePts = -1;
    mIsPause = false;

    createVideoExtractorThread();
    usleep(100000);

    LOG_I("create video extractor ok, continuous=%d", continuous);

    return 0;
}

int MyVideoExtractor::createVideoExtractorThread(int64_t startUs)
{
    if (!mInited) {
        return -1;
    }

    mThreadState = ThreadState::INIT;
    // mStartTimeUs = startUs;
    mStartTimeUs = 0;
    mSleepTimeUs = 0;

    if (mVideoExtractorThread != nullptr) {
        if (mVideoExtractorThread->joinable()) {
            mVideoExtractorThread->join();
        }
        mVideoExtractorThread = nullptr;
    }

    mVideoExtractorThread.reset(new std::thread(&MyVideoExtractor::videoExtractorThread, this));
    // SetPriority(mVideoExtractorThread->native_handle(), ThreadPriority::kRealtimePriority);

    while (mVideoExtractorThread != nullptr && mThreadState == ThreadState::INIT) {
        usleep(10000);
    }

    return 0;
}

int MyVideoExtractor::videoExtractorThread(void)
{
	LOG_I(".");
    int ret = 0;

    mThreadState = ThreadState::RUNNING;

    pthread_setname_np(pthread_self(), "vExtractor");    // 注意设置的线程名字不能超过15个字符

    int64_t last_read_time_us = 0;

    int mFrameCount;
    int mLastFrameCount = 0;
    uint64_t mLastFpsTime = 0;
    float mFps = 0;

    while (mThreadState == ThreadState::RUNNING) {
#if 1
        // if (mIsPause || !mPlay) {
        if (mIsPause) {
            LOG_I("sleep ..., mIsPause=%d, mPlay=%d", mIsPause, mPlay);
            do {
                usleep(500000);
            // } while ((mIsPause || !mPlay) && mThreadState);
            } while (mIsPause && (mThreadState == ThreadState::RUNNING));
            LOG_I("wake up ..., mIsPause=%d, mPlay=%d, mThreadState=%d", mIsPause, mPlay, mThreadState);

            if (mThreadState != ThreadState::RUNNING) {
                break;
            }
        }
#else
        if (mIsPause) {
            int64_t start_sleep_us = mExtractorUtils->now_us();
            LOG_I("pause ...");
            std::unique_lock<std::mutex> lock(mMutex);
            while (mIsPause) {
                if (! mThreadState) {
                    LOG_I("break ...");
                    break;
                    // return 0;
                }
                LOG_I("wait ...");
                mCondition.wait(lock);
                if (! mThreadState) {
                    return 0;
                }
            }

            mSleepTimeUs = mExtractorUtils->now_us() - start_sleep_us;
            LOG_I("wake up ..., mSleepTimeUs=%ld", mSleepTimeUs);
        }
#endif

        Utils::controlFrameRate(last_read_time_us, mFrameRateFps);
        Utils::debugShowFPS("camera", mFrameCount, mLastFrameCount, mLastFpsTime, mFps);

        ret = input();
        if (ret < 0) {
            usleep(1000);
            LOG_E("input failed, ret=%d", ret);
        } else if (ret == 1) {
            // usleep(12000);
            // continue;
            usleep(1000000 / mFrameRateFps);
        } else {
            // usleep(5000);
        }
        // else {
        //     ret = decode();
        //     if (ret < 0) {
        //         LOG_E("decode failed, ret=%d", ret);
        //     }
        // }

        ret = decode();
        if (ret < 0) {
            LOG_E("decode failed, ret=%d", ret);
        }

        if (mInputEof && mOutputEof) {
            if (mContinuous) {
                mInputEof = false;
                mOutputEof = false;
            } else {
                // mThreadState = false;
                break;
            }
        }
    }

    // if (mInputEof && mOutputEof) {
    //     sendMsg = true;
    // }

    releaseInner();

    LOG_I("exit videoExtractorThread");

    return 0;
}

int MyVideoExtractor::input(void)
{
    int ret = 0;
    AMediaCodec *decoder = mVideoDecoder;

    // int64_t start_ms = mExtractorUtils->now_ms();

#if 1
    int times = 4;
    do {
#endif

#if 0
        // 执行 AMediaExtractor_selectTrack 之后，就不需要调用 AMediaExtractor_getSampleTrackIndex 了
        int trackIndex = AMediaExtractor_getSampleTrackIndex(mVideoExtractor);
        if (trackIndex < 0) {
            LOG_E("AMediaExtractor_getSampleTrackIndex failed, trackIndex=%d", trackIndex);
            // mInputEof = true;
            break;
        }
#endif

        // 2. decodec
        // ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(decoder, TIMEOUT_US);
        ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(decoder, DEQUEUE_INPUT_TIMEOUT_US);
        if (inBufIdx >= 0) {
            // LOG_D("inBufIdx: %zu\n", inBufIdx);
            size_t inSize;
            uint8_t* inputBuf = AMediaCodec_getInputBuffer(decoder, inBufIdx, &inSize);
            // LOG_D("inSize: %zu\n", inSize);
            if (inputBuf != nullptr) {
                int err = 0;
                bool seek = false;
                // 读取一片或者一帧数据
                do {
                    ssize_t bufSize = AMediaExtractor_readSampleData(mVideoExtractor, &*mSampleBuf.begin(), mSampleBufSize);
                    if (bufSize > 0) {
                        if (bufSize <= inSize) {

                            // 读取时间戳
                            int64_t pts = AMediaExtractor_getSampleTime(mVideoExtractor);

                            mCacheFrameCnt++;
                            memcpy(inputBuf, &*mSampleBuf.begin(), bufSize);
                            if (pts == 0) {
                                LOG_I("bufSize=%zu, mCacheFrameCnt=%d, pts=%ld\n", bufSize, mCacheFrameCnt, pts);
                            }

                            media_status_t status = AMediaCodec_queueInputBuffer(decoder, inBufIdx, 0 /* offset */, bufSize, pts, 0);
                            if (status != AMEDIA_OK) {
                                LOG_E("AMediaCodec_queueInputBuffer failed, status=%d\n", status);
                                err = -1;
                            } else {

                            }
                        } else {
                            LOG_E("readSampleData failed bufSize=%zu, inSize=%zu", bufSize, inSize);
                            err = -1;
                        }
                        // 读取一帧后必须调用，提取下一帧
                        if (! AMediaExtractor_advance(mVideoExtractor)) {
                            LOG_E("advance failed");
                            if (!seek && mContinuous) {
                                LOG_I("------- video: seek to start -------");

                                if (AMediaExtractor_seekTo(mVideoExtractor, 0, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC) != AMEDIA_OK) {
                                    LOG_E("seek error");
                                }
                                // sleep(1);
                                // mStartTimeUs = mExtractorUtils->now_us();
                                seek = true;
                                continue;
                            }
                        }
                    } else {
                        if (!seek && mContinuous) {
                            LOG_I("------- video: seek to start -------");

                            if (AMediaExtractor_seekTo(mVideoExtractor, 0, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC) != AMEDIA_OK) {
                                LOG_E("seek error");
                            }
                            // sleep(1);
                            // mStartTimeUs = mExtractorUtils->now_us();
                            seek = true;
                            continue;
                        } else {
                            LOG_I("h264 bitstream is null, bufSize=%zu", bufSize);
                            mInputEof = true;
                            AMediaCodec_queueInputBuffer(decoder, inBufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        }
                    }
                } while (false);

                if (err < 0) {
                    ret = -1;
                } else {
                    ret = 0;
                    break;
                }
            } else {
                LOG_E("get Input Buffer failed, inputBuf=%p, inSize=%zu\n", inputBuf, inSize);
                ret = -1;
            }

            // break;
        } else {
            LOG_E("inBufIdx=%zd, times=%d, mCacheFrameCnt=%d", inBufIdx, times, mCacheFrameCnt);
            // logcat -s NdkMediaCodec

            switch (inBufIdx)
            {
            case AMEDIA_OK:
                LOG_E("AMEDIA_OK");
                break;
            case AMEDIA_ERROR_UNKNOWN:
                LOG_E("AMEDIA_ERROR_UNKNOWN");
                break;
            case AMEDIACODEC_INFO_TRY_AGAIN_LATER:
                LOG_E("AMEDIACODEC_INFO_TRY_AGAIN_LATER");
                break;
            default:
                LOG_E("unknown media_status_t");
                break;
            }

            AMediaCodec_flush(decoder);
            usleep(1000);
            // AMediaCodec_start(decoder);
            ret = -2;
            // return -1;
        }
#if 1
    } while (--times > 0);

    if (times <= 0) {
        LOG_E("hard codec dequeue input buffer failed still.\n");
        ret = -3;
#if 1
        usleep(10000);
        AMediaCodec_stop(decoder);
        usleep(10000);
        AMediaCodec_configure(decoder, mVideoFmt, NULL, NULL, 0);
        usleep(1000);
        AMediaCodec_start(decoder);
        usleep(10000);
#endif
    }
#endif

    // int64_t end_ms = mExtractorUtils->now_ms();
    // LOG_I("input delta time: %ld ms", end_ms - start_ms);

    return ret;
}


int MyVideoExtractor::decode(void)
{
    int ret = 0;
    AMediaCodec *decoder = mVideoDecoder;

    // if (mOutputEof) {
    //     return 0;
    // }

    // LOG_D("decode thread: trackIndex=%zu, decoder=%p", i, decoder);

    AMediaCodecBufferInfo info;

    // int64_t start_ms = mExtractorUtils->now_ms();
    // int64_t dequeue_1_ms = 0;
    // int64_t dequeue_2_ms = 0;
    bool bGotFrame = false;


    // 等待时间: 0 为不等待，-1 为一直等待，其余为时间单位
    ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, TIMEOUT_US);
    // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);
    // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, -1);
    // LOG_D("outBufIdx = %zu\n", outBufIdx);

    // dequeue_1_ms = mExtractorUtils->now_ms();
    if (outBufIdx >= 0) {
#if 1
// 若解码器无缓存情况下，送一帧，解码一帧，这样输出时间是平滑的
// 实现情况是，解码器会缓存帧，可能会现出送几帧，才输出一帧的情况
    // 这时候，有两个选择
    // 1. 如果缓存是固定的，让它缓存几帧，这样以后送入一帧，解码输出一帧，时间也是平滑的，
    //      但测试发现，把性能不足时，解码器又缓存了好多帧，
    // 2. 如果在 dequeueOutputBuffer 后，加一个循环读，把所有缓存帧，读出来，那输出时间就不平滑了，
    //      而且下次送入帧又缓存几帧，连续几百毫秒没输出，没法做到平滑输出
        do {
#endif
            size_t outSize = 0;
            uint8_t* outputBuf = AMediaCodec_getOutputBuffer(decoder, outBufIdx, &outSize);
            if (outputBuf != nullptr && info.size > 0) {
                bGotFrame = true;
                mCacheFrameCnt--;

                if (info.presentationTimeUs == 0) {
                    LOG_D("decode ok(%d), outSize=%zu, size=%d, offset=%d, width=%d, height=%d, stride=%d, fmt=%d, degrees=%d, pts=%ld", 
                        mCacheFrameCnt, outSize, info.size, info.offset, mWidth, mHeight, mWidthStride, mColorFmt, mRotationDegrees, info.presentationTimeUs);
                }

#if 1
                int tmpSize = 0;
                // frameworks/base/media/java/android/media/MediaCodecInfo.java 
                if (mColorFmt == 19) {          // public static final int COLOR_FormatYUV420Planar            = 19

                    tmpSize = MIN(mOriFrameSize, mFrameSize);
                    if (tmpSize > 0) {
                        std::shared_ptr<YuvFrame> yuv_frame = 
                            std::make_shared<YuvFrame>(tmpSize * 3/2, info.presentationTimeUs, mFrameRateFps, mWidth, mHeight);
                        
                        if (mIsFirstFramePts < 0) {
                            mIsFirstFramePts = info.presentationTimeUs;
                            LOG_I("start, mIsFirstFramePts=%ld", mIsFirstFramePts);
                        } else if (mIsFirstFramePts == info.presentationTimeUs) {       // loopback
                            LOG_I("loopback, mIsFirstFramePts=%ld", mIsFirstFramePts);
                        }
                        yuv_frame->mPts -= mIsFirstFramePts;

                        uint8_t* dst = yuv_frame->mBuf;
                        memcpy(dst, outputBuf, tmpSize);

                        // I420 TO NV21
                        // int qFrameSize = tmpSize / 4;
                        // int tempFrameSize = tmpSize * 5 / 4;
                        // for (int i = 0; i < qFrameSize; i++) {
                        //     dst[tmpSize + i * 2] = outputBuf[tempFrameSize + i]; // Cb (U)
                        //     dst[tmpSize + i * 2 + 1] = outputBuf[tmpSize + i]; // Cr (V)
                        // }

                        // I420 TO NV12
                        int qFrameSize = tmpSize / 4;
                        int tempFrameSize = tmpSize * 5 / 4;
                        for (int i = 0; i < qFrameSize; i++) {
                            dst[tmpSize + i * 2] = outputBuf[tmpSize + i]; // Cr (V)
                            dst[tmpSize + i * 2 + 1] = outputBuf[tempFrameSize + i]; // Cb (U)
                        }

                        sendYuvFrame(yuv_frame);
                    } else {
                        LOG_E("error, tmpSize=%d, mOriFrameSize=%d, mFrameSize=%d", tmpSize, mOriFrameSize, mFrameSize);
                    }

                } else if (mColorFmt == 21) {   // public static final int COLOR_FormatYUV420SemiPlanar        = 21;

                    tmpSize = MIN(mOriFrameSize, mFrameSize);
                    if (tmpSize > 0) {
                        std::shared_ptr<YuvFrame> yuv_frame = 
                            std::make_shared<YuvFrame>(tmpSize * 3/2, info.presentationTimeUs, mFrameRateFps, mWidth, mHeight);

                        if (mIsFirstFramePts < 0) {
                            mIsFirstFramePts = info.presentationTimeUs;
                            LOG_I("start, mIsFirstFramePts=%ld", mIsFirstFramePts);
                        } else if (mIsFirstFramePts == info.presentationTimeUs) {       // loopback
                            LOG_I("loopback, mIsFirstFramePts=%ld", mIsFirstFramePts);
                        }
                        yuv_frame->mPts -= mIsFirstFramePts;

                        // yuv_frame.size = MIN(mOriFrameSize, mFrameSize) * 3/2;
                        // yuv_frame.buf = new uint8_t[yuv_frame.size];

                        // LOG_D("tmpSize=%d, mOriFrameSize=%d, mFrameSize=%d", tmpSize, mOriFrameSize, mFrameSize);

                        // int64_t start11111 = mExtractorUtils->now_ms();
#if 1
                        memcpy(yuv_frame->mBuf, outputBuf, tmpSize * 3/2);
#else
                        uint8_t* dst = yuv_frame->mBuf;
                        memcpy(dst, outputBuf, tmpSize);


#if 0
                        uint8_t* uvBuf = outputBuf + mFrameSize;
                        uint32_t uvSize = MIN(mOriFrameSize >> 1, mFrameSize >> 1);
                        uint8_t *uBuf = dst  + mOriFrameSize;
                        uint8_t *vBuf = uBuf + (mOriFrameSize >> 2);

                        // NV12
                        memcpy(uBuf, uvBuf, uvSize);
#else
                        // NV12 TO NV21
                        int uLen = tmpSize/2;
                        for (int j = 0; j < uLen; j += 2) {
                            dst[tmpSize + j - 1] = outputBuf[j + tmpSize];
                            dst[tmpSize + j] = outputBuf[j + tmpSize - 1];
                        }
#endif
#endif
                        // int64_t start11111 = mExtractorUtils->now_ms();
                        // int64_t end2222222 = mExtractorUtils->now_ms();
                        // int64_t deltaTimeMs33333 = end2222222 - start11111;
                        // if (deltaTimeMs33333 > 3) {
                        //     LOG_I("NV12 To NV21, cost timeMs=%ld, tmpSize=%d", deltaTimeMs33333, tmpSize);
                        // }

                        sendYuvFrame(yuv_frame);
                    } else {
                        LOG_W("error, tmpSize=%d, mOriFrameSize=%d, mFrameSize=%d", tmpSize, mOriFrameSize, mFrameSize);
                    }
                }
#endif

#if 0
                mExtractorUtils->setVideoTimestamp(info.presentationTimeUs);

                if (info.presentationTimeUs == 0) {
                    // mStartTimeUs = mExtractorUtils->now_us();
                    // usleep(3000);
                } else {
                    int64_t sleep_us = info.presentationTimeUs + mSleepTimeUs - (mExtractorUtils->now_us() - mStartTimeUs);
                    if (sleep_us > 0 && sleep_us < 1000000) {
                        usleep(sleep_us);
                        LOG_I("sleep %ld ms", sleep_us / 1000);
                    } else {
                        LOG_I("sleep time(%ld ms) is too %s", sleep_us / 1000, sleep_us < 0 ? "small" : "long");
                        usleep(20000);
                    }
                }
                
#else
                // usleep(30000);
#endif
                ret = 0;
            } else {
                LOG_W("get Output Buffer failed, outputBuf=%p, outSize=%zu, info.size=%d", outputBuf, outSize, info.size);
                ret = -1;
            }
            // if (AMediaCodec_releaseOutputBuffer(decoder, outBufIdx, info.size != 0 /* render */) != AMEDIA_OK) {
            if (AMediaCodec_releaseOutputBuffer(decoder, outBufIdx, false /* render */) != AMEDIA_OK) {
                LOG_E("release output buffer failed");
            }

            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                LOG_I("video producer output EOS\n");
                // if (! mContinuous) {
                    mOutputEof = true;
                    mIsFirstFramePts = -1;
                // }
                // mStartTimeUs = mExtractorUtils->now_us();

                // break;
            }

#if 1
            // dequeue_2_ms = mExtractorUtils->now_ms();
            // outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);     // 这个超时时间会，影响延时
            outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, TIMEOUT_US);     // 这个超时时间会，影响延时
            // LOG_D("outBufIdx = %zu\n", outBufIdx);
        } while (outBufIdx >= 0);
#endif

#if 0
    } else {
#else
    }

    if (!bGotFrame && outBufIdx < 0) {
#endif
        switch (outBufIdx) {
            case AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED :
            {
                auto format = AMediaCodec_getOutputFormat(decoder);
                AMediaFormat_getInt32(format, "width", &mWidth);
                AMediaFormat_getInt32(format, "height", &mHeight);
                AMediaFormat_getInt32(format, "stride", &mWidthStride);
                AMediaFormat_getInt32(format, "color-format", &mColorFmt);
                AMediaFormat_getInt32(format, "rotation-degrees", &mRotationDegrees);
                
                // mColorFormat = getTTFormatFromMC(mColorFmt);
                AMediaFormat_delete(format);

                LOG_I("output format change, width=%d, height=%d, stride=%d, colorFmt=%d, degrees=%d", mWidth, mHeight, mWidthStride, mColorFmt, mRotationDegrees);

                if (mWidthStride != mWidth) {
                    LOG_I("stride(%d) != width(%d)", mWidthStride, mWidth);
                    if (mWidthStride == 0) {
                        mWidthStride = mWidth;
                    }
                }

                mFrameSize    = mWidthStride * mHeight;
                mOriFrameSize = mWidthStride * mOriHeight;
                
                ret = OUTPUT_FORMAT_CHANGED;
                break;
            }
            case AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED :
            {
                // size_t outSize;
                // uint8_t* outputBuf = AMediaCodec_getOutputBuffer(decoder, outBufIdx, &outSize);
                // LOG_I("codec format change, width: %d, height: %d, stride: %d, colorFmt: %d\n", mWidth, mHeight, stride, mColorFmt);
                
                LOG_I("output buffers change");

                ret = OUTPUT_BUFFERS_CHANGED;
                break;
            }
            default:
                // LOG_W("warning outBufIdx=%zu", outBufIdx);
                break;
        }
    }

    // int64_t end_ms = mExtractorUtils->now_ms();
    // if (bGotFrame) {
    //     LOG_D("output delta time: %ld, %ld, %ld, %ld", 
    //         end_ms - start_ms, dequeue_1_ms - start_ms, dequeue_2_ms - dequeue_1_ms, end_ms - dequeue_2_ms);
    // } else {
    //     LOG_D("output delta time: %ld ms", end_ms - start_ms);
    // }

    return ret;
}


int MyVideoExtractor::sendYuvFrame(const std::shared_ptr<YuvFrame>& frame)
{
    if (mFrameCallback != nullptr) {
        mFrameCallback(frame->mBuf, frame->mSize, frame->mPts);
    } else {
        LOG_E("mFrameCallback is null");
    }
    return 0;
}

/**
 * 是否处于播放状态
 * @return
 */
bool MyVideoExtractor::isPlaying(void) {
	LOG_I(".");

    if (!mInited) {
        return -1;
    }

    return mIsPlaying && !mIsPause;
}


void MyVideoExtractor::play(bool play) {
	LOG_I(".");

    if (!mInited) {
        return ;
    }

    mPlay = play;
    // if (! play) {
    //     mCondition.notify_one();
    // }
}

int MyVideoExtractor::pause(bool pause)
{
	LOG_I(".");

    if (!mInited) {
        LOG_E("not init");
        return -1;
    }

    mIsPause = pause;
    // if (! pause) {
    //     mCondition.notify_one();
    // }

    return 0;
}

int MyVideoExtractor::setContinuous(bool continuous)
{
	LOG_I(".");

    if (!mInited) {
        return -1;
    }

    mContinuous = continuous;

    return 0;
}


int MyVideoExtractor::fastForward(int64_t duration)
{
	LOG_I(".");

    if (!mInited) {
        return -1;
    }

    return 0;
}

int MyVideoExtractor::release(void)
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mInited) {
        LOG_W("video extractor is not initialized");
        return 0;
    }

	LOG_I("release start");

    while (mThreadState == ThreadState::INIT) {
        usleep(10000);
    }
    mThreadState = ThreadState::STOPPING;

    // if (mIsPause) {
    //     mCondition.notify_one();
    // }

    if (mVideoExtractorThread != nullptr && mVideoExtractorThread->joinable()) {
        mVideoExtractorThread->join();
        mVideoExtractorThread = nullptr;
        mThreadState = ThreadState::STOPPED;
    }

    usleep(100000);
    releaseInner();

	LOG_I("release end");

    return 0;
}


void MyVideoExtractor::releaseInner(void)
{
    LOG_I("release inner start");
    if (!mInited) {
        LOG_W("video extractor is not initialized");
        return ;
    }

    if (mVideoDecoder != nullptr) {
        AMediaCodec_flush(mVideoDecoder);
        AMediaCodec_stop(mVideoDecoder);
        AMediaCodec_delete(mVideoDecoder);
        mVideoDecoder = nullptr;
    }

    if (mVideoFmt != nullptr) {
        AMediaFormat_delete(mVideoFmt);
        mVideoFmt = nullptr;
    }

    if (mVideoExtractor != nullptr) {
        AMediaExtractor_delete(mVideoExtractor);
        mVideoExtractor = nullptr;
    }

    // if (fp_h264 != nullptr) {
    //     fclose(fp_h264);
    //     fp_h264 = nullptr;
    // }

    // property_set("vir.camera.config", "");
    // property_set("vir.camera.orien", "");
    // property_set("vir.camera.live", "");

    mSleepTimeUs = 0;
    mStartTimeUs = 0;
    mCacheFrameCnt = 0;
    mInputEof = false;
    mOutputEof = false;
    mContinuous = false;

    // mFrameRateFps = -1;
    // mDurationUs = -1;
    mInited = false;
    // mExtractorUtils->setVideoTimestamp(0);
    mCache = false;
    mIsPause = false;

    usleep(100000);
    LOG_I("release inner end");
}
