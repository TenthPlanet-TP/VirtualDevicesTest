#define LOG_TAG "NdkMediaExtractor"

#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string>

#include <thread>
#include <vector>
#include <map>

#include "SharedBufferClientInterface.h"
#include <sys/time.h>

#if 0

#include <utils/Log.h>
#include <binder/MemoryBase.h>
#include <binder/IPCThreadState.h>
#include <ndk/NdkMediaCodec.h>
#include <ndk/NdkMediaFormat.h>
#include <ndk/NdkMediaExtractor.h>

using namespace android;

#else

#include "utils.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaExtractor.h>

#endif


// #define TIMEOUT_US      5000
// #define TIMEOUT_US      20000
#define TIMEOUT_US      100000

// WebRTC queues input frames quickly in the beginning on the call. Wait for input buffers with a
// long timeout (500 ms) to prevent this from causing the codec to return an error.
#define DEQUEUE_INPUT_TIMEOUT_US            500000

// Dequeuing an output buffer will block until a buffer is available (up to 100 milliseconds).
// If this timeout is exceeded, the output thread will unblock and check if the decoder is still
// running.  If it is, it will block on dequeue again.  Otherwise, it will stop and release the
// MediaCodec.
#define DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US    100000


#define GOT_OUT_PUT             100
#define OUTPUT_FORMAT_CHANGED   101

#undef MIN
#define MIN(_a,_b) ((_a) > (_b) ? (_b) : (_a))


static int64_t kTimeout = 500ll;
// static int64_t kTimeout = 5000000ll;

static bool g_loop = true;
static bool g_again = true;


struct YuvFrame {
    YuvFrame(size_t size) 
        : mBuf(new uint8_t[size]),
            mSize(size)
    {
        // LOG_D("YuvFrame ctor called");
    }
    
    ~YuvFrame() {
        //  LOG_D("dtor YuvFrame called");
        if (mBuf != nullptr) {
            delete[] mBuf;
            mBuf = nullptr;
        }
    }

    uint8_t *mBuf;
    size_t mSize;
};

void handleSignal(int signo)
{
    if (SIGINT == signo || SIGTSTP == signo)
    {
        g_loop = false;
        LOG_I("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
    // exit(-1);
}


static uint32_t AACSampleRate[16] =
{
    97000, 88200, 64000, 48000,
    44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000,
    7350, 0, 0, 0 /*reserved */
};


/** 不包含CRC，所以 packetLen 需要一帧的长度 +7
 * @param packet    一帧数据（包含adts头长度）
 * @param packetLen 一帧数据（包含adts头）的长度，
 *
 */
int addADTStoPacket(uint8_t *packet, int packetLen)
{
    int profile = 2;    // AAC LC
    int freqIdx = 4;    // 44.1KHz
    int chnCfg = 2;     // CPE

    if (packet == nullptr || packetLen <= 7) {
        return -1;
    }

    uint8_t index = 0;
    for (index = 0; index < 16; index++) {
        // if (AACSampleRate[index] == mSampleRate) {
        //     break;
        // }
    }
    if (index == 16) {
        return -1; // error
    }

    freqIdx = index;

    packet[0] = (uint8_t) 0xFF;
    packet[1] = (uint8_t) 0xF9; // 1111 1001    // 1111 还是 syncword, 1001 第一个 1 代表 MPEG-2, 接着 00 为常量，最后一个 1，标识没有 CRC
    packet[2] = (uint8_t) (((profile - 1) << 6) + (freqIdx << 2) + (chnCfg >> 2));
    packet[3] = (uint8_t) (((chnCfg & 3) << 6) + (packetLen >> 11));
    packet[4] = (uint8_t) ((packetLen & 0x7FF) >> 3);
    packet[5] = (uint8_t) (((packetLen & 7) << 5) + 0x1F);
    packet[6] = (uint8_t) 0xFC;

    return 0;
}

int addADTStoPacket_test(uint8_t *packet, int packetLen, int profile, int sampleRate, int chnCfg)
{
    // int profile = 2;    // AAC LC
    int freqIdx = 4;    // 44.1KHz
    // int chnCfg = 2;     // CPE

    if (packet == nullptr || packetLen <= 7) {
        return -1;
    }

    uint8_t index = 0;
    for (index = 0; index < 16; index++) {
        if (AACSampleRate[index] == sampleRate) {
            break;
        }
    }
    if (index == 16) {
        return -1; // error
    }

    freqIdx = index;

    packet[0] = (uint8_t) 0xFF;
    packet[1] = (uint8_t) 0xF9;
    packet[2] = (uint8_t) (((profile - 1) << 6) + (freqIdx << 2) + (chnCfg >> 2));
    packet[3] = (uint8_t) (((chnCfg & 3) << 6) + (packetLen >> 11));
    packet[4] = (uint8_t) ((packetLen & 0x7FF) >> 3);
    packet[5] = (uint8_t) (((packetLen & 7) << 5) + 0x1F);
    packet[6] = (uint8_t) 0xFC;

    return 0;
}


int main(int argc, char *argv[])
{
	signal(SIGINT, handleSignal);
	signal(SIGTERM, handleSignal);

	if (argc != 2) {
		printf("Usage: %s <mp4 file>\n", argv[0]);
		return -1;
	}

    // Start Binder thread pool.  
    // MediaCodec needs to be able to receive messages from mediaserver.
    // sp<ProcessState> self = ProcessState::self();
    // self->startThreadPool();    

	// ------------------------------------------------------


    bool useAudio = true;
    bool useVideo = true;
    bool haveAudio = false;
    bool haveVideo = false; 
    bool isAudio = false;
    bool isVideo = false;

    bool eof = false;
    
    int32_t mWidth = 0, mHeight = 0;
    int64_t durationUs = -1;
    int32_t maxInputSize = -1;
    int32_t frameRateFps = -1;
    
    uint8_t *sampleBuf = nullptr;
    size_t sampleBufSize = 0;

    AMediaExtractor *mExtractor = nullptr;
    AMediaFormat *mVideoFmt = nullptr;
    AMediaFormat *mAudioFmt = nullptr;

    // char *videoMime = nullptr;
    // char *audioMime = nullptr;

    std::string videoMime;
    std::string audioMime;

    int32_t sampleRate = -1;
    int32_t channelCount = -1;
    int32_t aacProfile = -1;
    int32_t isAdts = -1;

    int64_t videoDurationUs = -1;
    int64_t audioDurationUs = -1;
    
    int mAudioTrack = -1;
    int mVideoTrack = -1;

    uint8_t *csd0 = nullptr, *csd1 = nullptr;
    size_t csd0Size = 0, csd1Size = 0;

    uint8_t *adts = nullptr;
    size_t adts_size = 0;

    uint64_t mFrameCnt;
    int mFps;

    int mOriFrameSize;
    int mFrameSize;
    int mOriHeight;
    int mFlagIndex;

    int32_t mLocalColorFMT;

    AMediaCodec* mVideoDecoder;
    AMediaCodec* mAudioDecoder;

    std::map<int, AMediaCodec*> decMap;

    bool mEof = false;

    // 获取文件后缀，过滤一些不支持的格式
    // ...


    int fd = open(argv[1], O_RDONLY | O_LARGEFILE);
    if (fd < 0) {
        printf("open %s failed\n", argv[1]);
        return -1;
    }

#if 0
    struct stat buf;
    if (!stat(argv[1], &buf)) {
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
        }
#if 1
        else if (!strncasecmp(mime, "video/", 6)) {
            mVideoFmt = format;
            AMediaExtractor_selectTrack(mExtractor, i);
            LOG_D("mime: %s, video track is : %zu\n", mime, i);
            mVideoTrack = i;
            isVideo = true;

            // videoMime = mime;
            videoMime.assign(mime);

            // break;
        }
// #elif 0
        else if (!strncasecmp(mime, "audio/", 6)) {
            mAudioFmt = format;
            AMediaExtractor_selectTrack(mExtractor, i);
            LOG_D("mime: %s, audio track is : %zu\n", mime, i);
            mAudioTrack = i;
            isAudio = true;

            // audioMime = mime;
            audioMime.assign(mime);

            // break;
        }
#endif
        if (isVideo && isAudio) {
            break;
        }
        // if (isVideo) {
        // 	break;
        // }
        // if (isAudio) {
        // 	break;
        // }
    }

    if (useAudio && !haveAudio && isAudio) {
    // if (useAudio && !haveAudio && isAudio && false) {
        haveAudio = true;

        bool ret = false;
        AMediaFormat_getInt32(mAudioFmt, "sample-rate", &sampleRate);
        AMediaFormat_getInt32(mAudioFmt, "channel-count", &channelCount);
        AMediaFormat_getInt32(mAudioFmt, "aac-profile", &aacProfile);
        AMediaFormat_getInt32(mAudioFmt, "max-input-size", &maxInputSize); // 得到能获取的有关音频的最大值
        AMediaFormat_getInt32(mAudioFmt, "is-adts", &isAdts);

#if 0
        size_t j = 0;
        uint8_t *csd_buf = nullptr;
        size_t csd_size = 0;

        std::vector<std::pair<uint8_t *, size_t>> vec;
        while (true) {
            char csd[12];
            snprintf(csd, sizeof(csd)/sizeof(csd[0]), "csd-%zu", j);
            if (! AMediaFormat_getBuffer(mAudioFmt, csd, reinterpret_cast<void **>(&csd_buf), &csd_size)) {
                break;
            }

            // vec.emplace_back(csd_buf, csd_size);
            vec.push_back(std::make_pair(csd_buf, csd_size));
            ++j;
        }
#else
        ret = AMediaFormat_getBuffer(mAudioFmt, "csd-0", reinterpret_cast<void **>(&adts), &adts_size);
        if (!ret) {
            LOG_E("AMediaFormat_getBuffer failed");
        }
#endif

        // sampleRate *= 2;
        LOG_D("create audio format: sampleRate=%d, channelCount=%d, aacProfile=%d, maxInputSize=%d, isAdts=%d", 
                sampleRate, channelCount, aacProfile, maxInputSize, isAdts);

        // if (maxInputSize > 0) {
        //     sampleBuf = new uint8_t[maxInputSize];
        //     sampleBufSize = maxInputSize;
        // }

        LOG_D("audioMime: %s", audioMime.c_str());      // audio/mp4a-latm
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

        LOG_D("inset audio");
        decMap.insert(std::make_pair(mAudioTrack, mAudioDecoder));
    }
    
    if (useVideo && !haveVideo && isVideo) {
        haveVideo = true;

        bool ret = false;
        AMediaFormat_getInt32(mVideoFmt, "width", &mWidth);
        AMediaFormat_getInt32(mVideoFmt, "height", &mHeight);
        AMediaFormat_getInt32(mVideoFmt, "max-input-size", &maxInputSize); // 得到能获取的有关视频的最大值
        AMediaFormat_getInt32(mVideoFmt, "frame-rate", &frameRateFps);
        AMediaFormat_getInt64(mVideoFmt, "durationUs", &durationUs);

#if 0
        size_t j = 0;
        uint8_t *csd_buf = nullptr;
        size_t csd_size = 0;

        // std:vector<T_CsdINfo> vec;
        std::vector<std::pair<uint8_t *, size_t>> vec;
        while (true) {
            char csd[12];
            snprintf(csd, sizeof(csd)/sizeof(csd[0]), "csd-%zu", j);
            if (! AMediaFormat_getBuffer(mVideoFmt, csd, reinterpret_cast<void **>(&csd_buf), &csd_size)) {
                break;
            }

            // vec.emplace_back(csd_buf, csd_size);
            vec.push_back(std::make_pair(csd_buf, csd_size));
            ++j;
        }
#else
        ret = AMediaFormat_getBuffer(mVideoFmt, "csd-0", reinterpret_cast<void **>(&csd0), &csd0Size);
        if (!ret) {
            LOG_E("AMediaFormat_getBuffer failed");
        }

        ret = AMediaFormat_getBuffer(mVideoFmt, "csd-1", reinterpret_cast<void **>(&csd1), &csd1Size);
        if (!ret) {
            LOG_E("AMediaFormat_getBuffer failed");
        }
#endif

        LOG_D("create video format: mWidth=%d, mHeight=%d, durationUs=%ld, maxInputSize=%d", mWidth, mHeight, durationUs, maxInputSize);

        if (maxInputSize > 0) {
            sampleBuf = new uint8_t[maxInputSize];
            sampleBufSize = maxInputSize;
        }

        LOG_D("videoMime: %s", videoMime.c_str());
        mVideoDecoder = AMediaCodec_createDecoderByType(videoMime.c_str());
        if (nullptr == mVideoDecoder) {
            LOG_E("AMediaCodec_createDecoderByType failed\n");
            delete[] sampleBuf;
            sampleBuf = nullptr;
            AMediaFormat_delete(mVideoFmt);
            AMediaExtractor_delete(mExtractor);
            return -1;
        }

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

        LOG_D("inset video");
        decMap.insert(std::make_pair(mVideoTrack, mVideoDecoder));
    }


    // AMediaExtractor_selectTrack(mExtractor, mVideoTrack);


    // https://github.com/huazi5D/MediaExtractorTest/blob/master/app/src/main/java/hz/mediaextractortest/MediaExtractorManager.java
    // 重新切换此信道，不然上面跳过了3帧,造成前面的帧数模糊
    // mMediaExtractor.unselectTrack(videoTrackIndex);
    // AMediaExtractor_selectTrack
    // AMediaExtractor_unselectTrack

    // AMediaExtractor_unselectTrack(mExtractor, mVideoTrack);
    // AMediaExtractor_selectTrack(mExtractor, mAudioTrack);

#if 0
    // std::thread audioThread([&]() {
    std::thread inputThread([&]() {

        while (g_loop) {
            int ret = 0;

            // usleep(20 * 1000);
            usleep(40 * 1000);
#if 1
            int times = 5;
            do {
#endif
                int trackIndex = AMediaExtractor_getSampleTrackIndex(mExtractor);
                if (trackIndex < 0) {
                    LOG_E("AMediaExtractor_getSampleTrackIndex failed, trackIndex=%d", trackIndex);
                    mEof = false;

                    // media_status_t status = AMediaCodec_queueInputBuffer(mDecoder, inBufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);

                    break;
                }

                // AMediaCodec* mDecoder = decMap.at(trackIndex);
                auto it = decMap.find(trackIndex);
                if (it == decMap.end()) {
                    LOG_E("can't find the AMediaCodec, times=%d", times);
                    continue;
                    // break;
                }
                AMediaCodec* mDecoder = it->second;
                LOG_D("trackIndex=%d, mDecoder=%p", trackIndex, mDecoder);

                // 2. decodec
                // ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(mDecoder, TIMEOUT_US);
                // ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(mDecoder, DEQUEUE_INPUT_TIMEOUT_US);
                ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(mDecoder, kTimeout);
                if (inBufIdx >= 0) {
                    // LOG_D("inBufIdx: %zu\n", inBufIdx);
                    size_t inSize;
                    uint8_t* inputBuf = AMediaCodec_getInputBuffer(mDecoder, inBufIdx, &inSize);
                    // LOG_D("inSize: %zu\n", inSize);
                    if (inputBuf != nullptr) {

                        // 读取一片或者一帧数据
                        ssize_t bufSize = AMediaExtractor_readSampleData(mExtractor, sampleBuf, sampleBufSize);
                        if (bufSize > 0 && bufSize <= inSize) {

                            // 读取时间戳
                            int64_t pts = AMediaExtractor_getSampleTime(mExtractor);
                            // LOG_D("pts=%ld, trackIndex=%d, bufSize=%zu\n", pts, trackIndex, bufSize);

                            memcpy(inputBuf, sampleBuf, bufSize);

                            media_status_t status = AMediaCodec_queueInputBuffer(mDecoder, inBufIdx, 0, bufSize, pts, 0);
                            if (status != AMEDIA_OK) {
                                LOG_E("AMediaCodec_queueInputBuffer failed, status=%d\n", status);
                            } else {

                            }

                            // 读取一帧后必须调用，提取下一帧
                            AMediaExtractor_advance(mExtractor);
                        } else {
                            LOG_E("h264 bitstream is null\n");
                            media_status_t status = AMediaCodec_queueInputBuffer(mDecoder, inBufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                            mEof = true;
                        }

                        ret = 0;
                    } else {
                        LOG_E("get Input Buffer failed, inputBuf=%p, inSize=%zu\n", inputBuf, inSize);
                        ret = -1;
                    }

                    break;
                } else {
                    LOG_E("inBufIdx=%zu, times=%d\n", inBufIdx, times);
                    // AMediaCodec_flush(mDecoder);
                    // AMediaCodec_start(mDecoder);
                    ret = -1;
                    // return -1;
                }
        #if 1        
            } while (--times > 0);

            if (times <= 0) {
                LOG_E("hard codec dequeue input buffer failed still. times=%d\n", times);
            }
        #endif

            if (mEof) {
                LOG_D("----------------eof------------------------");
                g_loop = false;
                break;
            }
        }
        LOG_D("input thread exit");
    });


    std::thread decodeThread([&]() {

        FILE *fp_yuv = fopen("/mnt/output.yuv", "wb");
        FILE *fp_pcm = fopen("/mnt/output.pcm", "wb");

        while (g_loop) {
            for (size_t i = 0; i < decMap.size(); i++) {
                AMediaCodec* mDecoder = decMap.at(i);
                LOG_D("decode thread: trackIndex=%zu, mDecoder=%p", i, mDecoder);

                int ret = 0;
                uint64_t pts;
                static int tmpSize = 0;

                AMediaCodecBufferInfo info;

                // 等待时间: 0 为不等待，-1 为一直等待，其余为时间单位
                // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, TIMEOUT_US);
                // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);
                // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, -1);
                ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, kTimeout);
                // LOG_D("outBufIdx = %zu\n", outBufIdx);
                if (outBufIdx >= 0) {
            #if 1
                    do {
            #endif
                        size_t outSize;
                        uint8_t* outputBuf = AMediaCodec_getOutputBuffer(mDecoder, outBufIdx, &outSize);
                        // LOG_D("outSize: %zu\n", outSize);
                        if (outputBuf != nullptr) {
                            // LOG_D("outSize=%zu, info.size=%d, info.offset=%d, pts=%ld", outSize, info.size, info.offset, info.presentationTimeUs);

                            // pts = info.presentationTimeUs;

                            if (i == mVideoTrack) {
                                LOG_D("decode video success, width: %d, height: %d, localColorFMT: %d, outSize=%zu", 
                                    mWidth, mHeight, mLocalColorFMT, outSize);

#if 1
                                // frameworks/base/media/java/android/media/MediaCodecInfo.java 
                                if (mLocalColorFMT == 19) {          // public static final int COLOR_FormatYUV420Planar            = 19

                                    tmpSize = MIN(mOriFrameSize, mFrameSize);
                                    std::shared_ptr<YuvFrame> yuv_frame = std::make_shared<YuvFrame>(tmpSize * 3/2);

                                    uint8_t* dst = yuv_frame->mBuf;
                                    memcpy(dst, outputBuf, tmpSize);

                                    // I420 TO NV21
                                    int qFrameSize = tmpSize / 4;
                                    int tempFrameSize = tmpSize * 5 / 4;
                                    for (int i = 0; i < qFrameSize; i++) {
                                        dst[tmpSize + i * 2] = outputBuf[tempFrameSize + i]; // Cb (U)
                                        dst[tmpSize + i * 2 + 1] = outputBuf[tmpSize + i]; // Cr (V)
                                    }
                                    
                                    // pushQueue(yuv_frame);

                                    if (fp_yuv != nullptr) {    
                                        fwrite(yuv_frame->mBuf, yuv_frame->mSize, 1, fp_yuv);
                                    }

                                } else if (mLocalColorFMT == 21) {   // public static final int COLOR_FormatYUV420SemiPlanar        = 21;
                                
                                    tmpSize = MIN(mOriFrameSize, mFrameSize);
                                    std::shared_ptr<YuvFrame> yuv_frame = std::make_shared<YuvFrame>(tmpSize * 3/2);
                                    // yuv_frame.size = MIN(mOriFrameSize, mFrameSize) * 3/2;
                                    // yuv_frame.buf = new uint8_t[yuv_frame.size];
                                
                                    uint8_t* dst = yuv_frame->mBuf;
                                    memcpy(dst, outputBuf, tmpSize);
                                    
                                    uint8_t* uvBuf = outputBuf + mFrameSize;
                                    uint32_t uvSize = MIN(mOriFrameSize >> 1, mFrameSize >> 1);
                                    uint8_t *uBuf = dst  + mOriFrameSize;
                                    uint8_t *vBuf = uBuf + (mOriFrameSize >> 2);

                                    int64_t start_ms = Utils::getCurrentTimeMs();

                                    // NV12 TO NV21
                                    for (int j = 0; j < tmpSize/2; j += 2) {
                                        dst[tmpSize + j - 1] = outputBuf[j + tmpSize];
                                        dst[tmpSize + j] = outputBuf[j + tmpSize - 1];
                                    }

                                    int64_t end_ms = Utils::getCurrentTimeMs();
                                    int64_t deltaTimeMs = end_ms - start_ms;
                                    if (deltaTimeMs > 2) {
                                        LOG_I("NV12 To NV21, cost timeMs=%ld, tmpSize=%d", deltaTimeMs, tmpSize);
                                    }

                                    // pushQueue(yuv_frame);

                                    // if (fp_yuv != nullptr) {
                                    //     fwrite(yuv_frame->mBuf, yuv_frame->mSize, 1, fp_yuv);
                                    // }

                                }
#endif
                            } else if (i == mAudioTrack) {
                                LOG_D("decode audio success, sampleRate: %d, channelCount: %d, outSize=%zu", 
                                    sampleRate, channelCount, outSize);
                                
                                if (info.size > 0 && info.size < outSize) {
                                    // memcpy(*buffer, outputBuf + info.offset, info.size);
                                    // if (fp_pcm != nullptr) {    
                                    //     fwrite(outputBuf + info.offset, info.size, 1, fp_pcm);
                                    // }
                                }
                            }

                            ret = 0;
                        } else {
                            LOG_E("get Output Buffer failed, outputBuf=%p, outSize=%zu\n", outputBuf, outSize);
                            ret = -1;
                        }
                        // AMediaCodec_releaseOutputBuffer(mDecoder, outBufIdx, info.size != 0);
                        AMediaCodec_releaseOutputBuffer(mDecoder, outBufIdx, false /* render */);

                        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                            LOG_I("video producer output EOS\n");
                            // eof = true;
                            // mOutputEof = true;

                            break;
                        }
            #if 1
                        // outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);     // 这个超时时间会，影响延时
                        outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, kTimeout);     // 这个超时时间会，影响延时
                        // LOG_D("outBufIdx = %zu\n", outBufIdx);
                    } while (outBufIdx >= 0);
            #endif

            #if 0
                } else {
            #else
                }

                if (outBufIdx < 0) {
            #endif
                    switch (outBufIdx) {
                        case AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED:
                        {
                            if (i == mVideoTrack) {
                                auto format = AMediaCodec_getOutputFormat(mDecoder);
                                AMediaFormat_getInt32(format, "width", &mWidth);
                                AMediaFormat_getInt32(format, "height", &mHeight);
                                AMediaFormat_getInt32(format, "color-format", &mLocalColorFMT);
                                // mColorFormat = getTTFormatFromMC(mLocalColorFMT);
                                int32_t stride = 0;
                                AMediaFormat_getInt32(format, "stride", &stride);
                                AMediaFormat_delete(format);

                                LOG_I("codec format change, width: %d, height: %d, stride: %d, localColorFMT: %d\n", mWidth, mHeight, stride, mLocalColorFMT);

                                if (stride == 0) {
                                    stride = mWidth;
                                }
                                // mLineSize[0] = stride;
                                mFrameSize    = stride * mHeight;
                                mOriFrameSize = stride * mOriHeight;

                                // return OUTPUT_FORMAT_CHANGED;
                            } else if (i == mAudioTrack) {

                                auto format = AMediaCodec_getOutputFormat(mDecoder);
                                AMediaFormat_getInt32(format, "sample-rate", &sampleRate);
                                AMediaFormat_getInt32(format, "channel-count", &channelCount);
                                AMediaFormat_getInt32(format, "aac-profile", &aacProfile);
                                AMediaFormat_getInt32(format, "max-input-size", &maxInputSize); // 得到能获取的有关音频的最大值
                                AMediaFormat_getInt32(format, "is-adts", &isAdts);

                                LOG_D("create audio format: sampleRate=%d, channelCount=%d, aacProfile=%d, maxInputSize=%d, isAdts=%d", 
                                        sampleRate, channelCount, aacProfile, maxInputSize, isAdts);
                            }
                        }
                        case AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED:
                            LOG_I("output buffers changes");

                            // AMediaCodec_getOutputBuffer
                            // if (i == mAudioTrack) {
                            //     uint8_t* outputBuf = AMediaCodec_getOutputBuffer(mDecoder, outBufIdx, &outSize);
                            //     LOG_D("output video codec format: width: %d, height: %d, localColorFMT: %d, outSize=%zu", 
                            //         mWidth, mHeight, mLocalColorFMT, outSize);
                            // } else if (i == mVideoTrack) {
                            //     uint8_t* outputBuf = AMediaCodec_getOutputBuffer(mDecoder, outBufIdx, &outSize);
                            //     LOG_D("output video codec format: width: %d, height: %d, localColorFMT: %d, outSize=%zu", 
                            //         mWidth, mHeight, mLocalColorFMT, outSize);
                            // }

                            ret = -1;
                            break;
                        default:
                            ret = -1;
                            break;
                    }
                }

            }

            // LOG_D("-----------------------decMap.size=%zu", decMap.size());
            if (decMap.size() == 0) {
                // sleep(1);
                usleep(50);
            }
        }

        fclose(fp_yuv);
        fclose(fp_pcm);

        LOG_D("output] thread exit");
    });

#else

    std::thread singleThread([&]() {
        // while (g_loop) {
        while (g_again) {
            int ret = 0;

            // usleep(20 * 1000);
            usleep(40 * 1000);
#if 1
            int times = 5;
            do {
#endif
                int trackIndex = AMediaExtractor_getSampleTrackIndex(mExtractor);
                if (trackIndex < 0) {
                    LOG_E("AMediaExtractor_getSampleTrackIndex failed, trackIndex=%d", trackIndex);
                    mEof = false;

                    // media_status_t status = AMediaCodec_queueInputBuffer(mDecoder, inBufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);

                    break;
                }

                // AMediaCodec* mDecoder = decMap.at(trackIndex);
                auto it = decMap.find(trackIndex);
                if (it == decMap.end()) {
                    LOG_E("can't find the AMediaCodec, times=%d", times);
                    continue;
                    // break;
                }
                AMediaCodec* mDecoder = it->second;
                // LOG_D("trackIndex=%d, mDecoder=%p", trackIndex, mDecoder);

                usleep(500 * 1000);

                // 2. decodec
                // ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(mDecoder, TIMEOUT_US);
                // ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(mDecoder, DEQUEUE_INPUT_TIMEOUT_US);
                ssize_t inBufIdx = AMediaCodec_dequeueInputBuffer(mDecoder, kTimeout);
                if (inBufIdx >= 0) {
                    // LOG_D("inBufIdx: %zu\n", inBufIdx);
                    size_t inSize;
                    uint8_t* inputBuf = AMediaCodec_getInputBuffer(mDecoder, inBufIdx, &inSize);
                    // LOG_D("inSize: %zu\n", inSize);
                    if (inputBuf != nullptr) {

                        // 读取一片或者一帧数据
                        ssize_t bufSize = AMediaExtractor_readSampleData(mExtractor, sampleBuf, sampleBufSize);
                        if (bufSize > 0 && bufSize <= inSize) {

                            // 读取时间戳
                            int64_t pts = AMediaExtractor_getSampleTime(mExtractor);
                            // LOG_D("pts=%ld, inSize=%zu, bufSize=%zu\n", pts, inSize, bufSize);

                            memcpy(inputBuf, sampleBuf, bufSize);

                            media_status_t status = AMediaCodec_queueInputBuffer(mDecoder, inBufIdx, 0, bufSize, pts, 0);
                            if (status != AMEDIA_OK) {
                                LOG_E("AMediaCodec_queueInputBuffer failed, status=%d\n", status);
                            } else {

                            }

                            // 读取一帧后必须调用，提取下一帧
                            AMediaExtractor_advance(mExtractor);
                        } else {
                            LOG_E("h264 bitstream is null\n");
                            media_status_t status = AMediaCodec_queueInputBuffer(mDecoder, inBufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                            mEof = true;
                        }

                        ret = 0;
                    } else {
                        LOG_E("get Input Buffer failed, inputBuf=%p, inSize=%zu\n", inputBuf, inSize);
                        ret = -1;
                    }

                    break;
                } else {
                    LOG_E("inBufIdx=%zu, times=%d\n", inBufIdx, times);
                    // AMediaCodec_flush(mDecoder);
                    // AMediaCodec_start(mDecoder);
                    ret = -1;
                    // return -1;
                }
        #if 1        
            } while (--times > 0);

            if (times <= 0) {
                LOG_E("hard codec dequeue input buffer failed still. times=%d\n", times);
            }
        #endif

            if (mEof) {
                LOG_D("----------------eof------------------------");
                g_loop = false;
                break;
            }

            // output
            // for (size_t i = 0; i < decMap.size(); i++) {
            //     AMediaCodec* mDecoder = decMap.at(i);
            //     LOG_D("decode thread: trackIndex=%zu, mDecoder=%p", i, mDecoder);
            for (const auto &it : decMap) {
                int i = it.first;
                AMediaCodec* mDecoder = it.second;
                // LOG_D("decode thread: trackIndex=%d, mDecoder=%p", i, mDecoder);

                int ret = 0;
                uint64_t pts;
                static int tmpSize = 0;

                AMediaCodecBufferInfo info;

                // 等待时间: 0 为不等待，-1 为一直等待，其余为时间单位
                // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, TIMEOUT_US);
                // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);
                // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, -1);
                ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, kTimeout);
                // ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, 1000000);
                // LOG_D("outBufIdx = %zu\n", outBufIdx);
                if (outBufIdx >= 0) {
            #if 1
                    do {
            #endif
                        size_t outSize;
                        uint8_t* outputBuf = AMediaCodec_getOutputBuffer(mDecoder, outBufIdx, &outSize);
                        // LOG_D("outSize: %zu\n", outSize);
                        if (outputBuf != nullptr) {
                            // LOG_D("outSize=%zu, info.size=%d, info.offset=%d, pts=%ld", outSize, info.size, info.offset, info.presentationTimeUs);

                            // pts = info.presentationTimeUs;

                            if (i == mVideoTrack) {
                                LOG_D("decode video success, width: %d, height: %d, localColorFMT: %d, outSize=%zu", 
                                    mWidth, mHeight, mLocalColorFMT, outSize);

#if 1
                                // frameworks/base/media/java/android/media/MediaCodecInfo.java 
                                if (mLocalColorFMT == 19) {          // public static final int COLOR_FormatYUV420Planar            = 19

                                    tmpSize = MIN(mOriFrameSize, mFrameSize);
                                    std::shared_ptr<YuvFrame> yuv_frame = std::make_shared<YuvFrame>(tmpSize * 3/2);

                                    uint8_t* dst = yuv_frame->mBuf;
                                    memcpy(dst, outputBuf, tmpSize);

                                    // I420 TO NV21
                                    int qFrameSize = tmpSize / 4;
                                    int tempFrameSize = tmpSize * 5 / 4;
                                    for (int i = 0; i < qFrameSize; i++) {
                                        dst[tmpSize + i * 2] = outputBuf[tempFrameSize + i]; // Cb (U)
                                        dst[tmpSize + i * 2 + 1] = outputBuf[tmpSize + i]; // Cr (V)
                                    }
                                    
                                    // pushQueue(yuv_frame);

                                    // if (fp_yuv != nullptr) {    
                                    //     fwrite(yuv_frame->mBuf, yuv_frame->mSize, 1, fp_yuv);
                                    // }

                                } else if (mLocalColorFMT == 21) {   // public static final int COLOR_FormatYUV420SemiPlanar        = 21;
                                
                                    tmpSize = MIN(mOriFrameSize, mFrameSize);
                                    std::shared_ptr<YuvFrame> yuv_frame = std::make_shared<YuvFrame>(tmpSize * 3/2);
                                    // yuv_frame.size = MIN(mOriFrameSize, mFrameSize) * 3/2;
                                    // yuv_frame.buf = new uint8_t[yuv_frame.size];
                                
                                    uint8_t* dst = yuv_frame->mBuf;
                                    memcpy(dst, outputBuf, tmpSize);
                                    
                                    uint8_t* uvBuf = outputBuf + mFrameSize;
                                    uint32_t uvSize = MIN(mOriFrameSize >> 1, mFrameSize >> 1);
                                    uint8_t *uBuf = dst  + mOriFrameSize;
                                    uint8_t *vBuf = uBuf + (mOriFrameSize >> 2);

                                    int64_t start_ms = Utils::getCurrentTimeMs();

                                    // NV12 TO NV21
                                    for (int j = 0; j < tmpSize/2; j += 2) {
                                        dst[tmpSize + j - 1] = outputBuf[j + tmpSize];
                                        dst[tmpSize + j] = outputBuf[j + tmpSize - 1];
                                    }

                                    int64_t end_ms = Utils::getCurrentTimeMs();
                                    int64_t deltaTimeMs = end_ms - start_ms;
                                    if (deltaTimeMs > 2) {
                                        // LOG_I("NV12 To NV21, cost timeMs=%ld, tmpSize=%d", deltaTimeMs, tmpSize);
                                    }

                                    // pushQueue(yuv_frame);

                                    // if (fp_yuv != nullptr) {
                                    //     fwrite(yuv_frame->mBuf, yuv_frame->mSize, 1, fp_yuv);
                                    // }

                                }
#endif
                            } else if (i == mAudioTrack) {
                                LOG_D("decode audio success, sampleRate: %d, channelCount: %d, outSize=%zu", 
                                    sampleRate, channelCount, outSize);
                                
                                if (info.size > 0 && info.size < outSize) {
                                    // memcpy(*buffer, outputBuf + info.offset, info.size);
                                    // if (fp_pcm != nullptr) {    
                                    //     fwrite(outputBuf + info.offset, info.size, 1, fp_pcm);
                                    // }
                                }
                            }

                            ret = 0;
                        } else {
                            LOG_E("get Output Buffer failed, outputBuf=%p, outSize=%zu\n", outputBuf, outSize);
                            ret = -1;
                        }
                        // AMediaCodec_releaseOutputBuffer(mDecoder, outBufIdx, info.size != 0);
                        AMediaCodec_releaseOutputBuffer(mDecoder, outBufIdx, false /* render */);

                        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                            LOG_I("video producer output EOS\n");
                            // eof = true;
                            // mOutputEof = true;

                            break;
                        }
            #if 1
                        // outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);     // 这个超时时间会，影响延时
                        outBufIdx = AMediaCodec_dequeueOutputBuffer(mDecoder, &info, kTimeout);     // 这个超时时间会，影响延时
                        // LOG_D("outBufIdx = %zu\n", outBufIdx);
                    } while (outBufIdx >= 0);
            #endif

            #if 0
                } else {
            #else
                }

                if (outBufIdx < 0) {
            #endif
                    switch (outBufIdx) {
                        case AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED:
                        {
                            if (i == mVideoTrack) {
                                auto format = AMediaCodec_getOutputFormat(mDecoder);
                                AMediaFormat_getInt32(format, "width", &mWidth);
                                AMediaFormat_getInt32(format, "height", &mHeight);
                                AMediaFormat_getInt32(format, "color-format", &mLocalColorFMT);
                                // mColorFormat = getTTFormatFromMC(mLocalColorFMT);
                                int32_t stride = 0;
                                AMediaFormat_getInt32(format, "stride", &stride);
                                AMediaFormat_delete(format);

                                LOG_I("codec format change, width: %d, height: %d, stride: %d, localColorFMT: %d\n", mWidth, mHeight, stride, mLocalColorFMT);

                                if (stride == 0) {
                                    stride = mWidth;
                                }
                                // mLineSize[0] = stride;
                                mFrameSize    = stride * mHeight;
                                mOriFrameSize = stride * mOriHeight;

                                // return OUTPUT_FORMAT_CHANGED;
                            } else if (i == mAudioTrack) {

                                auto format = AMediaCodec_getOutputFormat(mDecoder);
                                AMediaFormat_getInt32(format, "sample-rate", &sampleRate);
                                AMediaFormat_getInt32(format, "channel-count", &channelCount);
                                AMediaFormat_getInt32(format, "aac-profile", &aacProfile);
                                AMediaFormat_getInt32(format, "max-input-size", &maxInputSize); // 得到能获取的有关音频的最大值
                                AMediaFormat_getInt32(format, "is-adts", &isAdts);

                                LOG_D("create audio format: sampleRate=%d, channelCount=%d, aacProfile=%d, maxInputSize=%d, isAdts=%d", 
                                        sampleRate, channelCount, aacProfile, maxInputSize, isAdts);
                            }
                        }
                        case AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED:
                            LOG_I("output buffers changes");

                            // AMediaCodec_getOutputBuffer
                            // if (i == mAudioTrack) {
                            //     uint8_t* outputBuf = AMediaCodec_getOutputBuffer(mDecoder, outBufIdx, &outSize);
                            //     LOG_D("output video codec format: width: %d, height: %d, localColorFMT: %d, outSize=%zu", 
                            //         mWidth, mHeight, mLocalColorFMT, outSize);
                            // } else if (i == mVideoTrack) {
                            //     uint8_t* outputBuf = AMediaCodec_getOutputBuffer(mDecoder, outBufIdx, &outSize);
                            //     LOG_D("output video codec format: width: %d, height: %d, localColorFMT: %d, outSize=%zu", 
                            //         mWidth, mHeight, mLocalColorFMT, outSize);
                            // }

                            ret = -1;
                            break;
                        default:
                            ret = -1;
                            break;
                    }
                }

            }

            // LOG_D("-----------------------decMap.size=%zu", decMap.size());
            if (decMap.size() == 0) {
                // sleep(1);
                usleep(50000);
            }

        }
    });

#endif


    getchar();
    getchar();

    // g_loop = false;
    g_again = false;

    LOG_D("break...\n");

    // audioThread.join();
    // videoThread.join();
    // decodeThread.join();
    // inputThread.join();
    singleThread.join();



    // if (fd > 0) {
    //     close(fd);
    //     fd = -1;
    // }

    if (sampleBuf != nullptr) {
        delete[] sampleBuf;
        sampleBuf = nullptr;
    }

    if (mAudioFmt != nullptr) {
        AMediaFormat_delete(mAudioFmt);
        mAudioFmt = nullptr;
    }
    
    if (mVideoFmt != nullptr) {
        AMediaFormat_delete(mVideoFmt);
        mVideoFmt = nullptr;
    } 

    if (mVideoDecoder != nullptr) {
        AMediaCodec_stop(mVideoDecoder);
        AMediaCodec_delete(mVideoDecoder);
        mVideoDecoder = nullptr;
    }

    if (mAudioDecoder != nullptr) {
        AMediaCodec_stop(mAudioDecoder);
        AMediaCodec_delete(mAudioDecoder);
        mAudioDecoder = nullptr;
    }

    if (mExtractor != nullptr) {
        AMediaExtractor_delete(mExtractor);
        mExtractor = nullptr;
        LOG_D("delete...");
    }

    LOG_D("client : test finish ...\n");

	return 0;
}
 