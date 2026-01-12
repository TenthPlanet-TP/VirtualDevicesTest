
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include <string>
#include <thread>
#include <vector>
#include <map>

#include "audio_extractor.h"
#include "utils.h"


// #ifdef MODULE_NAME
// #undef MODULE_NAME
// #endif
// #define MODULE_NAME "AudioExtractor"


// #define TIMEOUT_US      5000
// #define TIMEOUT_US      20000
#define TIMEOUT_US        10000

// WebRTC queues input frames quickly in the beginning on the call. Wait for input buffers with a
// long timeout (500 ms) to prevent this from causing the codec to return an error.
#define DEQUEUE_INPUT_TIMEOUT_US            500000
// #define DEQUEUE_INPUT_TIMEOUT_US            50000

// Dequeuing an output buffer will block until a buffer is available (up to 100 milliseconds).
// If this timeout is exceeded, the output thread will unblock and check if the decoder is still
// running.  If it is, it will block on dequeue again.  Otherwise, it will stop and release the
// MediaCodec.
#define DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US    100000


#define OUTPUT_BUFFERS_CHANGED              100
#define OUTPUT_FORMAT_CHANGED               101


#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))



MyAudioExtractor::MyAudioExtractor()
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

MyAudioExtractor::~MyAudioExtractor() {
    release();
}

int MyAudioExtractor::create(const char *fileName, bool continuous)
{
	LOG_I(".");

	bool useAudio = true;
    bool haveAudio = false;
	bool isAudio = false;
	
	int64_t durationUs = -1;
    int32_t frameRateFps = -1;
	
    std::string audioMime;

    // int64_t audioDurationUs = -1;
	
    uint8_t *adts = nullptr;
    size_t adts_size = 0;


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
    printf("size of file : %ld\n", fileSize);
#endif

    release();

    std::lock_guard<std::mutex> lock(mMutex);

    mExtractor = AMediaExtractor_new();
    if (mExtractor == nullptr) {
        LOG_E("AMediaExtractor_new error.\n");
        close(fd);
        return -1;
    }

    // media_status_t ret = AMediaExtractor_setDataSourceFd(mExtractor, fd, 0, buf.st_size);
    media_status_t ret = AMediaExtractor_setDataSourceFd(mExtractor, fd, 0, fileSize);
    if (AMEDIA_OK != ret) {
        LOG_E("AMediaExtractor_setDataSourceFd failed.\n");
        AMediaExtractor_delete(mExtractor);
        close(fd);
        return -1;
    }

    if (fd > 0) {
        close(fd);
        fd = -1;
    }

    size_t trackCount = AMediaExtractor_getTrackCount(mExtractor);
    if (trackCount <= 0) {
        LOG_E("AMediaExtractor_getTrackCount failed.\n");
        AMediaExtractor_delete(mExtractor);
        return -1;
    }

    for (size_t i = 0; i < trackCount; ++i) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(mExtractor, i);
        if (format == nullptr) {
            AMediaExtractor_delete(mExtractor);
            LOG_E("AMediaExtractor_getTrackFormat failed.\n");
            return -1;
        }

        const char *mime;
        if (!AMediaFormat_getString(format, "mime", &mime)) {
            continue;
        } else if (!strncasecmp(mime, "audio/", 6)) {
            mAudioFmt = format;
            AMediaExtractor_selectTrack(mExtractor, i);
			LOG_D("mime: %s, audio track is : %zu\n", mime, i);
			mAudioTrack = i;
			isAudio = true;

            // audioMime = mime;
            audioMime.assign(mime);
            // break;
        }
		
		if (isAudio) {
			break;
		}
    }

	if (useAudio && !haveAudio && isAudio) {
		haveAudio = true;

        // bool ret = false;
		AMediaFormat_getInt32(mAudioFmt, "sample-rate", &mSampleRate);
		AMediaFormat_getInt32(mAudioFmt, "channel-count", &mChannelCount);
		AMediaFormat_getInt32(mAudioFmt, "aac-profile", &mAacProfile);
		AMediaFormat_getInt32(mAudioFmt, "max-input-size", &mMaxInputSize); // 得到能获取的有关音频的最大值
		// AMediaFormat_getInt32(mAudioFmt, "is-adts", &isAdts);

        if (mMaxInputSize > 0) {
            mSampleBuf.reserve(mMaxInputSize);
            mSampleBufSize = mMaxInputSize;
        }

        // mSampleRate *= 2;
        LOG_I("create audio format: mSampleRate=%d, mChannelCount=%d, mAacProfile=%d, mMaxInputSize=%d", 
                mSampleRate, mChannelCount, mAacProfile, mMaxInputSize);

        // LOG_D("audioMime: %s", audioMime.c_str());      // audio/mp4a-latm
        mAudioDecoder = AMediaCodec_createDecoderByType(audioMime.c_str());
        if (nullptr == mAudioDecoder) {
            LOG_E("AMediaCodec_createDecoderByType failed\n");
            AMediaFormat_delete(mAudioFmt);
            AMediaExtractor_delete(mExtractor);
            return -1;
        }

        media_status_t status = AMediaCodec_configure(mAudioDecoder, mAudioFmt, NULL, NULL, 0);
        if (status != AMEDIA_OK) {
            LOG_E("AMediaCodec_configure error\n");
            AMediaFormat_delete(mAudioFmt);
            AMediaCodec_delete(mAudioDecoder);
            mAudioDecoder = NULL;
            return -1;
        }

        status = AMediaCodec_start(mAudioDecoder);
        if (status != AMEDIA_OK) {
            LOG_E("AMediaCodec_start error\n");
            AMediaFormat_delete(mAudioFmt);
            AMediaCodec_delete(mAudioDecoder);
            mAudioDecoder = NULL;
            return -1;
        }
	} else {
        LOG_E("can not find audio track\n");
        AMediaFormat_delete(mAudioFmt);
        AMediaCodec_delete(mAudioDecoder);
        return -1;
    }

    // m_fp = fopen("/mnt/output.pcm", "wb");
    // if (m_fp == nullptr) {
    //     LOG_E("can't open file");
    // }

    // https://github.com/huazi5D/MediaExtractorTest/blob/master/app/src/main/java/hz/mediaextractortest/MediaExtractorManager.java
    // //重新切换此信道，不然上面跳过了3帧,造成前面的帧数模糊
    // mMediaExtractor.unselectTrack(videoTrackIndex);

    // AMediaExtractor_unselectTrack(mExtractor, mVideoTrack);
    // AMediaExtractor_selectTrack(mExtractor, mAudioTrack);    

    mCacheFrameCnt = 0;
    mContinuous = continuous;
    mInited = true;
    // mIsPause = true;
    mPeriodSize = 0;
    mCache = true;
    mIsFirstFramePts = -1;
    mIsPause = false;

    createAudioExtractorThread();
    usleep(100000);

    LOG_I("create audio extractor ok");

    return 0;
}


int MyAudioExtractor::createAudioExtractorThread(int64_t startUs)
{
    if (!mInited) {
        return -1;
    }

    // mThreadState = true;
    mThreadState = ThreadState::INIT;

    // mStartTimeUs = startUs;
    mStartTimeUs = 0;
    mSleepTimeUs = 0;

    if (mAudioExtractorThread != nullptr && mAudioExtractorThread->joinable()) {
        mAudioExtractorThread->join();
        mAudioExtractorThread = nullptr;
    }

    mAudioExtractorThread.reset(new std::thread(&MyAudioExtractor::audioExtractorThread, this));

    while (mAudioExtractorThread != nullptr && mThreadState == ThreadState::INIT) {
        usleep(10000);
    }

    return 0;
}

int MyAudioExtractor::audioExtractorThread(void)
{
	LOG_I(".");
    int ret = 0;

    // if (mStartTimeUs == 0) {
    //     mStartTimeUs = mExtractorUtils->now_us();
    // }

    mThreadState = ThreadState::RUNNING;

    pthread_setname_np(pthread_self(), "aExtractor");    // 注意设置的线程名字不能超过15个字符

    int64_t last_read_time_us = 0;

    int mFrameCount;
    int mLastFrameCount = 0;
    uint64_t mLastFpsTime = 0;
    float mFps = 0;

    // 48000 / 960 = 50
    // 44100 / 1024 = 43.2
    // int frameRateFps = mSampleRate / mPeriodSize;

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

        Utils::controlFrameRate(last_read_time_us, mSampleRate / (mPeriodSize > 0 ? mPeriodSize : 1024));
        Utils::debugShowFPS("audio", mFrameCount, mLastFrameCount, mLastFpsTime, mFps);

        ret = input();
        if (ret < 0) {
            usleep(1000);
            LOG_E("input failed, ret=%d", ret);
        } else if (ret == 1) {
            // usleep(12000);
            // continue;
            usleep(mPeriodSize > 0 ? (1000000 / mPeriodSize) : 20000);
        } else {
            // usleep(6000);
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

        if (mInputEof || mOutputEof) {
            if (mContinuous) {
                mInputEof = false;
                mOutputEof = false;
            } else {
                // mThreadState = false;
                break;
            }
        }
    }

    // mThreadState = STOPPING;
    // mThreadState = STOPPED;

    releaseInner();

    LOG_I("exit audioExtractorThread");

    return 0;
}

int MyAudioExtractor::input(void)
{
    int ret = 0;
    AMediaCodec *decoder = mAudioDecoder;

    if (mInputEof) {
        return 0;
    }

#if 1
    int times = 6;
    do {
#endif

#if 0
        int trackIndex = AMediaExtractor_getSampleTrackIndex(mExtractor);
        if (trackIndex < 0) {
            LOG_E("AMediaExtractor_getSampleTrackIndex failed, trackIndex=%d", trackIndex);
            mInputEof = true;
            break;
        }
        // LOG_D("trackIndex=%d, decoder=%p", trackIndex, decoder);
#endif

        // 2. decodec
        // ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(decoder, TIMEOUT_US);
        ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(decoder, DEQUEUE_INPUT_TIMEOUT_US);
        // ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(decoder, kTimeout);
        if (inBufIdx >= 0) {
            // LOG_D("inBufIdx: %zu\n", inBufIdx);
            size_t inSize;
            uint8_t* inputBuf = AMediaCodec_getInputBuffer(decoder, inBufIdx, &inSize);
            // LOG_D("inSize: %zu\n", inSize);
            if (inputBuf != nullptr) {
                int err = 0;
                bool seek = false;
                do {
                    // 读取一片或者一帧数据
                    ssize_t bufSize = AMediaExtractor_readSampleData(mExtractor, &*mSampleBuf.begin(), mSampleBufSize);
                    if (bufSize > 0 && bufSize <= inSize) {

                        // 读取时间戳
                        int64_t pts = AMediaExtractor_getSampleTime(mExtractor);
                        // LOG_D("pts=%ld, trackIndex=%d, bufSize=%zu\n", pts, trackIndex, bufSize);

                        mCacheFrameCnt++;
                        memcpy(inputBuf, &*mSampleBuf.begin(), bufSize);

                        if (pts == 0) {
                            LOG_I("bufSize=%zu, mCacheFrameCnt=%d, pts=%ld\n", bufSize, mCacheFrameCnt, pts);
                        }

                        media_status_t status = AMediaCodec_queueInputBuffer(decoder, inBufIdx, 0, bufSize, pts, 0);
                        if (status != AMEDIA_OK) {
                            LOG_E("AMediaCodec_queueInputBuffer failed, status=%d\n", status);
                            err = -1;
                        } else {

                        }

                        // 读取一帧后必须调用，提取下一帧
                        if (! AMediaExtractor_advance(mExtractor)) {
                            LOG_E("advance failed");
                            if (!seek && mContinuous) {
                                LOG_I("------- audio: seek to start -------");

                                if (AMediaExtractor_seekTo(mExtractor, 0, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC) != AMEDIA_OK) {
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
                            LOG_I("------- audio: seek to start -------");

                            if (AMediaExtractor_seekTo(mExtractor, 0, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC) != AMEDIA_OK) {
                                LOG_E("seek error");
                            }
                            // sleep(1);
                            // mStartTimeUs = mExtractorUtils->now_us();
                            seek = true;
                            continue;
                        } else {
                            LOG_I("bitstream is null\n");
                            mInputEof = true;
                            AMediaCodec_queueInputBuffer(decoder, inBufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        }
                        // AMediaCodec_queueInputBuffer(decoder, inBufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
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
            LOG_E("inBufIdx=%zu, times=%d\n", inBufIdx, times);
            AMediaCodec_flush(decoder);
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
        AMediaCodec_configure(decoder, mAudioFmt, NULL, NULL, 0);
        usleep(1000);
        AMediaCodec_start(decoder);
        usleep(10000);
#endif
    }
#endif

    return ret;
}


int MyAudioExtractor::decode(void)
{
    int ret = 0;

    AMediaCodec *decoder = mAudioDecoder;
    AMediaCodecBufferInfo info;

    // int64_t start_ms = mExtractorUtils->now_ms();
    // int64_t dequeue_1_ms = 0;
    // int64_t dequeue_2_ms = 0;
    bool bGotFrame = false;

    // LOG_D("decode thread: trackIndex=%zu, decoder=%p", i, decoder);

    // 等待时间: 0 为不等待，-1 为一直等待，其余为时间单位
    ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, TIMEOUT_US);
    // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);
    // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, -1);
    // LOG_D("outBufIdx = %zu\n", outBufIdx);

    // dequeue_1_ms = mExtractorUtils->now_ms();
    if (outBufIdx >= 0) {
#if 1
        do {
#endif
            uint64_t pts;
            size_t outSize;
            uint8_t* outputBuf = AMediaCodec_getOutputBuffer(decoder, outBufIdx, &outSize);
            if (outputBuf != nullptr && info.size > 0) {
                bGotFrame = true;
                mCacheFrameCnt--;

                if (info.presentationTimeUs == 0) {
                    LOG_I("decode ok(%d), outSize=%zu, size=%d, offset=%d, sampleRate=%d, channel=%d, pts=%ld", 
                        mCacheFrameCnt, outSize, info.size, info.offset, mSampleRate, mChannelCount, info.presentationTimeUs);
                }

                if (info.size > 0 && info.size <= outSize) {
                // if (info.size == 4096 && info.size <= outSize) {
#if 1
                    // if (m_fp != nullptr) {    
                    //     fwrite(outputBuf + info.offset, info.size, 1, m_fp);
                    // }

                    int16_t periodSize = info.size / mChannelCount / 2;
                    if (mPeriodSize != periodSize) {
                        mPeriodSize = periodSize;
                        // char pcm_config[24];
                        // snprintf(pcm_config, ARRAY_SIZE(pcm_config), "%d.%d", mSampleRate, mPeriodSize);
                        // LOG_I("set pcm config: %s", pcm_config);
                        // property_set("vir.audio.pcm_config_in", pcm_config);
                    }
                    std::shared_ptr<PcmFrame> pcm_frame = std::make_shared<PcmFrame>(info.size, info.presentationTimeUs);

                    if (mIsFirstFramePts < 0) {
                        mIsFirstFramePts = info.presentationTimeUs;
                        LOG_I("start, mIsFirstFramePts=%ld", mIsFirstFramePts);
                    } else if (mIsFirstFramePts == info.presentationTimeUs) {       // loopback
                        LOG_I("loopback, mIsFirstFramePts=%ld", mIsFirstFramePts);
                    }
                    pcm_frame->mPts -= mIsFirstFramePts;

                    // LOG_I("pcm_frame->mPts=%ld", pcm_frame->mPts);
                    memcpy(pcm_frame->mBuf, outputBuf, info.size);
                    sendPcmFrame(pcm_frame);
#endif
                } else {
                    // int32_t = sampleTime =  info.size / mSampleRate;    
                }

#if 0
                mExtractorUtils->setAudioTimestamp(info.presentationTimeUs);
                if (info.presentationTimeUs == 0) {
                    mStartTimeUs = mExtractorUtils->now_us();
                    usleep(3000);
                }

                int64_t sleep_us = info.presentationTimeUs + mSleepTimeUs - (mExtractorUtils->now_us() - mStartTimeUs);
                if (sleep_us > 0 && sleep_us < 1000000) {
                    LOG_I("sleep %ld ms", sleep_us / 1000);
                    usleep(sleep_us);
                } else {
                    LOG_I("sleep time(%ld ms) is too long", sleep_us / 1000);
                    usleep(10000);
                }
#else
                // usleep(22000);
#endif

                ret = 0;
            } else {
                LOG_W("get Output Buffer failed, outputBuf=%p, outSize=%zu, info.size=%d", outputBuf, outSize, info.size);
                ret = -1;
            }
            // AMediaCodec_releaseOutputBuffer(decoder, outBufIdx, info.size != 0 /* render */);
            AMediaCodec_releaseOutputBuffer(decoder, outBufIdx, false /* render */);

            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                LOG_I("audio producer output EOS\n");
                mOutputEof = true;
                mIsFirstFramePts = -1;
                // break;
            }
#if 1
            // dequeue_2_ms = mExtractorUtils->now_ms();
            outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, TIMEOUT_US);     // 这个超时时间会，影响延时
            // outBufIdx = AMediaCodec_dequeueOutputBuffer(decoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);     // 这个超时时间会，影响延时
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
            case AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED:
            {
                auto format = AMediaCodec_getOutputFormat(decoder);
                AMediaFormat_getInt32(format, "sample-rate", &mSampleRate);
                AMediaFormat_getInt32(format, "channel-count", &mChannelCount);
                AMediaFormat_getInt32(format, "aac-profile", &mAacProfile);
                AMediaFormat_getInt32(format, "max-input-size", &mMaxInputSize); // 得到能获取的有关音频的最大值
                // AMediaFormat_getInt32(format, "is-adts", &isAdts);

                LOG_I("output format change: mSampleRate=%d, mChannelCount=%d, mAacProfile=%d, mMaxInputSize=%d", 
                        mSampleRate, mChannelCount, mAacProfile, mMaxInputSize);

                ret = OUTPUT_FORMAT_CHANGED;
                break;
            }
            case AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED:
            {
                // size_t outSize;
                // uint8_t* outputBuf = AMediaCodec_getOutputBuffer(decoder, outBufIdx, &outSize);
                
                LOG_I("output buffers change");

                ret = OUTPUT_BUFFERS_CHANGED;
                break;
            }
            default:
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

int MyAudioExtractor::sendPcmFrame(const std::shared_ptr<PcmFrame>& frame)
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
bool MyAudioExtractor::isPlaying(void) {
	LOG_I(".");

    if (!mInited) {
        return -1;
    }

    return mIsPlaying && !mIsPause;
}

int MyAudioExtractor::pause(bool pause)
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

void MyAudioExtractor::play(bool play) {
	LOG_I(".");

    if (!mInited) {
        return ;
    }

    mPlay = play;
    // if (! play) {
    //     mCondition.notify_one();
    // }
}

int MyAudioExtractor::setContinuous(bool continuous)
{
	LOG_I(".");

    if (!mInited) {
        return -1;
    }

    mContinuous = continuous;

    return 0;
}

int MyAudioExtractor::fastForward(int64_t duration)
{
	LOG_I(".");

    if (!mInited) {
        return -1;
    }

    return 0;
}

void MyAudioExtractor::releaseInner(void)
{
    if (!mInited) {
        LOG_W("audio extractor is not initialized");
        return ;
    }

    if (mAudioDecoder != nullptr) {
        AMediaCodec_stop(mAudioDecoder);
        AMediaCodec_delete(mAudioDecoder);
        mAudioDecoder = nullptr;
    }

    if (mAudioFmt != nullptr) {
        AMediaFormat_delete(mAudioFmt);
        mAudioFmt = nullptr;
    }
    
    if (mExtractor != nullptr) {
        AMediaExtractor_delete(mExtractor);
        mExtractor = nullptr;
    }

    // if (m_fp != nullptr) {
    //     fclose(m_fp);
    //     m_fp = nullptr;
    // }

    // property_set("vir.audio.pcm_config_in", "");

    mSleepTimeUs = 0;
    mStartTimeUs = 0;
    mCacheFrameCnt = 0;
    // mIsPause = true;
    mInputEof = false;
    mOutputEof = false;
    mContinuous = false;
    // mDurationUs = -1;
    mInited = false;
    mPeriodSize = 0;
    // mExtractorUtils->setAudioTimestamp(0);
    mCache = false;
    mIsPause = false;

    usleep(100000);
}

int MyAudioExtractor::release(void)
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mInited) {
        LOG_W("audio extractor is not initialized");
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
    // mIsPause = false;

    if (mAudioExtractorThread != nullptr && mAudioExtractorThread->joinable()) {
        mAudioExtractorThread->join();
        mAudioExtractorThread = nullptr;
        mThreadState = ThreadState::STOPPED;
    }


    usleep(100000);
    releaseInner();

	LOG_I("release end");

    return 0;
}
