#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if SPATIAL_USE_SOFA
#include <mysofa.h>
#endif

#define LOG_TAG "SpatialNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kFIRSize = 256;
constexpr std::uint32_t kMaxHrtfTaps = 4096;

struct HrtfProfile {
    bool loaded = false;
    std::vector<float> left;
    std::vector<float> right;
    float azimuth = 0.0f;
    float elevation = 0.0f;
};

struct HrtfPoint {
    float azimuth = 0.0f;
    float elevation = 0.0f;
    std::vector<float> left;
    std::vector<float> right;
};

class SpatialAudioEngine {
public:
    SpatialAudioEngine() = default;

    bool init(int sampleRate, int channels) {
        sampleRate_ = sampleRate > 0 ? sampleRate : 48000;
        channels_ = channels > 0 ? channels : 2;
        if (channels_ != 2) {
            LOGE("Only stereo is supported in this engine.");
            return false;
        }

        history_.assign(kFIRSize, 0.0f);
        leftHistory_.assign(kFIRSize, 0.0f);
        rightHistory_.assign(kFIRSize, 0.0f);
        generateFallbackProfile();
#if SPATIAL_USE_SOFA
        LOGI("Spatial engine initialized with libmysofa: sr=%d, channels=%d", sampleRate_, channels_);
#else
        LOGI("Spatial engine initialized in fallback HRTF mode: sr=%d, channels=%d", sampleRate_, channels_);
#endif
        return true;
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        profile_.loaded = false;
        profile_.left.clear();
        profile_.right.clear();
        history_.assign(kFIRSize, 0.0f);
        leftHistory_.assign(kFIRSize, 0.0f);
        rightHistory_.assign(kFIRSize, 0.0f);
        sampleRate_ = 48000;
        channels_ = 2;
    }

    void setParameters(float azimuthDeg, float elevationDeg, float proximity) {
        std::lock_guard<std::mutex> lock(mutex_);
        azimuth_ = azimuthDeg;
        elevation_ = std::clamp(elevationDeg, -90.0f, 90.0f);
        proximity_ = std::clamp(proximity, 0.0f, 1.0f);
        updateProfileFromParameters();
    }

    bool loadHrtfFromFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOGE("HRTF file not found: %s", path.c_str());
            generateFallbackProfile();
            return false;
        }

        const std::string magicHeader = "HRTF1";
        char header[5] = {0};
        file.read(header, 5);
        if (file.gcount() != 5 || (std::string(header) != magicHeader && std::string(header) != "HRTF2")) {
            LOGE("Invalid HRTF header in %s", path.c_str());
            generateFallbackProfile();
            return false;
        }

        if (std::string(header) == "HRTF2") {
            std::uint32_t pointCount = 0;
            std::uint32_t tapCount = 0;
            file.read(reinterpret_cast<char*>(&pointCount), sizeof(pointCount));
            file.read(reinterpret_cast<char*>(&tapCount), sizeof(tapCount));
            if (!file || pointCount == 0 || pointCount > 4096 || tapCount == 0 || tapCount > kMaxHrtfTaps) {
                LOGE("Invalid HRTF2 dimensions in %s", path.c_str());
                generateFallbackProfile();
                return false;
            }

            profiles_.clear();
            profiles_.reserve(pointCount);
            for (std::uint32_t point = 0; point < pointCount; ++point) {
                HrtfPoint value;
                value.left.resize(tapCount);
                value.right.resize(tapCount);
                file.read(reinterpret_cast<char*>(&value.azimuth), sizeof(value.azimuth));
                file.read(reinterpret_cast<char*>(&value.elevation), sizeof(value.elevation));
                file.read(reinterpret_cast<char*>(value.left.data()), static_cast<std::streamsize>(tapCount * sizeof(float)));
                file.read(reinterpret_cast<char*>(value.right.data()), static_cast<std::streamsize>(tapCount * sizeof(float)));
                if (!file) {
                    LOGE("Truncated HRTF2 file: %s", path.c_str());
                    generateFallbackProfile();
                    return false;
                }
                profiles_.push_back(std::move(value));
            }
            profile_.loaded = true;
            updateProfileFromParameters();
            LOGI("Dense HRTF loaded from %s (%u points, %u taps)", path.c_str(), pointCount, tapCount);
            return true;
        }

        profiles_.clear();

        std::uint32_t leftCount = 0;
        std::uint32_t rightCount = 0;
        file.read(reinterpret_cast<char*>(&leftCount), sizeof(leftCount));
        file.read(reinterpret_cast<char*>(&rightCount), sizeof(rightCount));

        if (!file || leftCount == 0 || rightCount == 0 ||
            leftCount > kMaxHrtfTaps || rightCount > kMaxHrtfTaps) {
            LOGE("Invalid HRTF tap counts in %s", path.c_str());
            generateFallbackProfile();
            return false;
        }

        profile_.left.resize(leftCount, 0.0f);
        profile_.right.resize(rightCount, 0.0f);
        file.read(reinterpret_cast<char*>(profile_.left.data()), static_cast<std::streamsize>(leftCount * sizeof(float)));
        file.read(reinterpret_cast<char*>(profile_.right.data()), static_cast<std::streamsize>(rightCount * sizeof(float)));

        if (!file) {
            LOGE("Truncated HRTF file: %s", path.c_str());
            generateFallbackProfile();
            return false;
        }

        profile_.loaded = true;
        profile_.azimuth = azimuth_;
        profile_.elevation = elevation_;

        LOGI("HRTF loaded from %s (%zu left taps, %zu right taps)", path.c_str(), profile_.left.size(), profile_.right.size());
        return true;
    }

    int processBuffer(const float* input, float* output, int frames) {
        if (input == nullptr || output == nullptr || frames <= 0) {
            return -1;
        }

        const int stride = channels_;
        for (int i = 0; i < frames; ++i) {
            const int base = i * stride;
            const float inL = input[base];
            const float inR = input[base + 1];

            const float leftSample = processSample(inL, inR, true, leftHistory_);
            const float rightSample = processSample(inR, inL, false, rightHistory_);

            output[base] = leftSample;
            output[base + 1] = rightSample;
        }

        return 0;
    }

private:
    void generateFallbackProfile() {
        profiles_.clear();
        profile_.loaded = false;
        profile_.left.assign(kFIRSize, 0.0f);
        profile_.right.assign(kFIRSize, 0.0f);

        for (int i = 0; i < kFIRSize; ++i) {
            const float norm = static_cast<float>(i) / static_cast<float>(kFIRSize - 1);
            const float envelope = std::exp(-4.0f * norm);
            const float leftGain = 0.75f + 0.25f * std::cos(azimuth_ * kPi / 180.0f);
            const float rightGain = 0.75f - 0.25f * std::cos(azimuth_ * kPi / 180.0f);
            profile_.left[i] = envelope * leftGain * (i == 0 ? 1.0f : 0.75f / (1.0f + i * 0.25f));
            profile_.right[i] = envelope * rightGain * (i == 0 ? 1.0f : 0.75f / (1.0f + i * 0.25f));
        }
    }

    void updateProfileFromParameters() {
        if (!profiles_.empty()) {
            const HrtfPoint* nearest = &profiles_.front();
            float bestDistance = std::numeric_limits<float>::max();
            for (const HrtfPoint& point : profiles_) {
                float azimuthDistance = std::fabs(point.azimuth - azimuth_);
                azimuthDistance = std::min(azimuthDistance, 360.0f - azimuthDistance);
                const float elevationDistance = point.elevation - elevation_;
                const float distance = azimuthDistance * azimuthDistance + elevationDistance * elevationDistance;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    nearest = &point;
                }
            }
            profile_.left = nearest->left;
            profile_.right = nearest->right;
            profile_.azimuth = nearest->azimuth;
            profile_.elevation = nearest->elevation;
            profile_.loaded = true;
        } else if (!profile_.loaded) {
            generateFallbackProfile();
        }
    }

    float processSample(float mono, float cross, bool leftChannel,
                        std::vector<float>& history) {
        const float angle = azimuth_ * kPi / 180.0f;
        const float elevation = elevation_ * kPi / 180.0f;
        const float frontBack = std::cos(angle);
        const float lateral = std::sin(angle);
        const float vertical = std::sin(elevation);
        const float proximityGain = 1.0f - proximity_ * 0.55f;
        const float spread = 0.55f + 0.45f * (1.0f - proximity_);

        float dry = mono * proximityGain;
        float secondary = cross * 0.12f * (1.0f - proximity_);

        float directGain = spread * (0.7f + (leftChannel ? 0.85f : -0.85f) * lateral + 0.25f * frontBack + 0.30f * vertical);
        float out = dry * directGain + secondary;

        history.insert(history.begin(), out);
        history.pop_back();

        float filtered = 0.0f;
        const std::vector<float>& hrtf = leftChannel ? profile_.left : profile_.right;
        for (int i = 0; i < std::min<int>(hrtf.size(), kFIRSize); ++i) {
            filtered += history[i] * hrtf[i];
        }

        return filtered * (leftChannel ? 0.9f : 0.9f);
    }

    std::mutex mutex_;
    int sampleRate_ = 48000;
    int channels_ = 2;
    float azimuth_ = 0.0f;
    float elevation_ = 45.0f;
    float proximity_ = 0.5f;
    std::vector<float> history_;
    std::vector<float> leftHistory_;
    std::vector<float> rightHistory_;
    HrtfProfile profile_;
    std::vector<HrtfPoint> profiles_;
};

std::unique_ptr<SpatialAudioEngine> gEngine;

class PcmRingBuffer {
public:
    static constexpr size_t kCapacity = 1u << 18;

    size_t write(const float* input, size_t count) {
        size_t written = 0;
        while (written < count) {
            const size_t writeIndex = writeIndex_.load(std::memory_order_relaxed);
            const size_t readIndex = readIndex_.load(std::memory_order_acquire);
            if (writeIndex - readIndex >= kCapacity) break;
            buffer_[writeIndex % kCapacity] = input[written++];
            writeIndex_.store(writeIndex + 1, std::memory_order_release);
        }
        return written;
    }

    size_t read(float* output, size_t count) {
        size_t read = 0;
        while (read < count) {
            const size_t readIndex = readIndex_.load(std::memory_order_relaxed);
            const size_t writeIndex = writeIndex_.load(std::memory_order_acquire);
            if (readIndex == writeIndex) break;
            output[read++] = buffer_[readIndex % kCapacity];
            readIndex_.store(readIndex + 1, std::memory_order_release);
        }
        return read;
    }

    void clear() {
        const size_t writeIndex = writeIndex_.load(std::memory_order_acquire);
        readIndex_.store(writeIndex, std::memory_order_release);
    }

private:
    std::array<float, kCapacity> buffer_{};
    std::atomic<size_t> writeIndex_{0};
    std::atomic<size_t> readIndex_{0};
};

PcmRingBuffer gInputBuffer;
std::shared_ptr<oboe::AudioStream> gOutputStream;

class AudioCallback final : public oboe::AudioStreamDataCallback {
public:
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* /*stream*/, void* audioData,
                                          int32_t numFrames) override {
        auto* output = static_cast<float*>(audioData);
        const size_t sampleCount = static_cast<size_t>(numFrames) * 2;
        if (sampleCount > input_.size()) {
            std::fill(output, output + sampleCount, 0.0f);
            return oboe::DataCallbackResult::Continue;
        }
        std::fill(input_.begin(), input_.begin() + sampleCount, 0.0f);
        gInputBuffer.read(input_.data(), sampleCount);
        if (gEngine == nullptr || gEngine->processBuffer(input_.data(), output, numFrames) != 0) {
            std::fill(output, output + static_cast<size_t>(numFrames) * 2, 0.0f);
        }
        return oboe::DataCallbackResult::Continue;
    }

private:
    std::array<float, 4096> input_{};
};

std::shared_ptr<AudioCallback> gAudioCallback;

#if SPATIAL_USE_SOFA
bool convertSofaToHrtf(const std::string& sourcePath, const std::string& targetPath, int sampleRate) {
    int filterLength = 0;
    int error = MYSOFA_OK;
    struct MYSOFA_EASY* easy = mysofa_open(sourcePath.c_str(), static_cast<float>(sampleRate), &filterLength, &error);
    if (easy == nullptr || error != MYSOFA_OK || filterLength <= 0 || filterLength > static_cast<int>(kMaxHrtfTaps)) {
        LOGE("Could not open SOFA %s (error %d, filter length %d)", sourcePath.c_str(), error, filterLength);
        if (easy != nullptr) mysofa_close(easy);
        return false;
    }

    constexpr float kAzimuthStep = 15.0f;
    constexpr float kElevationStep = 15.0f;
    constexpr int kAzimuthPoints = 24;
    constexpr int kElevationPoints = 13;
    const std::uint32_t pointCount = kAzimuthPoints * kElevationPoints;
    std::ofstream file(targetPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    const std::uint32_t tapCount = static_cast<std::uint32_t>(filterLength);
    file.write("HRTF2", 5);
    file.write(reinterpret_cast<const char*>(&pointCount), sizeof(pointCount));
    file.write(reinterpret_cast<const char*>(&tapCount), sizeof(tapCount));
    for (int elevationIndex = 0; elevationIndex < kElevationPoints; ++elevationIndex) {
        const float elevation = -90.0f + elevationIndex * kElevationStep;
        for (int azimuthIndex = 0; azimuthIndex < kAzimuthPoints; ++azimuthIndex) {
            const float azimuth = -180.0f + azimuthIndex * kAzimuthStep;
            const float azimuthRadians = azimuth * kPi / 180.0f;
            const float elevationRadians = elevation * kPi / 180.0f;
            const float x = std::cos(elevationRadians) * std::cos(azimuthRadians);
            const float y = std::cos(elevationRadians) * std::sin(azimuthRadians);
            const float z = std::sin(elevationRadians);
            std::vector<float> left(static_cast<size_t>(filterLength));
            std::vector<float> right(static_cast<size_t>(filterLength));
            float delayLeft = 0.0f;
            float delayRight = 0.0f;
            mysofa_getfilter_float(easy, x, y, z, left.data(), right.data(), &delayLeft, &delayRight);
            file.write(reinterpret_cast<const char*>(&azimuth), sizeof(azimuth));
            file.write(reinterpret_cast<const char*>(&elevation), sizeof(elevation));
            file.write(reinterpret_cast<const char*>(left.data()), static_cast<std::streamsize>(left.size() * sizeof(float)));
            file.write(reinterpret_cast<const char*>(right.data()), static_cast<std::streamsize>(right.size() * sizeof(float)));
            if (!file) {
                mysofa_close(easy);
                return false;
            }
        }
    }
    mysofa_close(easy);
    return file.good();
}
#endif
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_initEngine(JNIEnv* env, jobject /*thiz*/, jint sampleRate, jint channels) {
    try {
        gEngine = std::make_unique<SpatialAudioEngine>();
        return gEngine && gEngine->init(sampleRate, channels) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) {
        LOGE("initEngine exception: %s", e.what());
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_releaseEngine(JNIEnv* env, jobject /*thiz*/) {
    gEngine.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_setParameters(JNIEnv* env, jobject /*thiz*/, jfloat azimuth, jfloat elevation, jfloat proximity) {
    if (gEngine) {
        gEngine->setParameters(azimuth, elevation, proximity);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_loadHrtfFromFile(JNIEnv* env, jobject /*thiz*/, jstring path) {
    if (gEngine == nullptr || path == nullptr) {
        return JNI_FALSE;
    }

    const char* cPath = env->GetStringUTFChars(path, nullptr);
    if (cPath == nullptr) {
        return JNI_FALSE;
    }

    const bool ok = gEngine->loadHrtfFromFile(cPath);
    env->ReleaseStringUTFChars(path, cPath);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_convertSofaToHrtf(JNIEnv* env, jobject /*thiz*/, jstring sourcePath, jstring targetPath, jint sampleRate) {
#if SPATIAL_USE_SOFA
    if (sourcePath == nullptr || targetPath == nullptr || sampleRate <= 0) return JNI_FALSE;
    const char* source = env->GetStringUTFChars(sourcePath, nullptr);
    const char* target = env->GetStringUTFChars(targetPath, nullptr);
    if (source == nullptr || target == nullptr) {
        if (source != nullptr) env->ReleaseStringUTFChars(sourcePath, source);
        if (target != nullptr) env->ReleaseStringUTFChars(targetPath, target);
        return JNI_FALSE;
    }
    const bool ok = convertSofaToHrtf(source, target, sampleRate);
    env->ReleaseStringUTFChars(sourcePath, source);
    env->ReleaseStringUTFChars(targetPath, target);
    return ok ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_startAudio(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (gEngine == nullptr) return JNI_FALSE;
    if (gOutputStream != nullptr) return JNI_TRUE;

    gAudioCallback = std::make_shared<AudioCallback>();
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(2)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Shared)
        ->setDataCallback(gAudioCallback.get());

    oboe::Result result = builder.openStream(gOutputStream);
    if (result != oboe::Result::OK) {
        LOGE("Oboe could not open output stream: %s", oboe::convertToText(result));
        gOutputStream.reset();
        gAudioCallback.reset();
        return JNI_FALSE;
    }

    result = gOutputStream->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("Oboe could not start output stream: %s", oboe::convertToText(result));
        gOutputStream->close();
        gOutputStream.reset();
        gAudioCallback.reset();
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_stopAudio(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (gOutputStream != nullptr) {
        gOutputStream->requestStop();
        gOutputStream->close();
        gOutputStream.reset();
    }
    gAudioCallback.reset();
    gInputBuffer.clear();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_enqueueAudio(JNIEnv* env, jobject /*thiz*/, jfloatArray samples) {
    if (samples == nullptr) return 0;
    const jsize count = env->GetArrayLength(samples);
    if (count <= 0) return 0;
    jfloat* data = env->GetFloatArrayElements(samples, nullptr);
    if (data == nullptr) return 0;
    const size_t written = gInputBuffer.write(data, static_cast<size_t>(count));
    env->ReleaseFloatArrayElements(samples, data, JNI_ABORT);
    return static_cast<jint>(written);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tuapp_spatialaudio_NativeSpatialAudio_processBuffer(JNIEnv* env, jobject /*thiz*/, jfloatArray input, jfloatArray output, jint frames) {
    if (gEngine == nullptr || input == nullptr || output == nullptr || frames <= 0) {
        return -1;
    }

    jsize inputSize = env->GetArrayLength(input);
    jsize outputSize = env->GetArrayLength(output);
    if (inputSize < frames * 2 || outputSize < frames * 2) {
        return -2;
    }

    jfloat* in = env->GetFloatArrayElements(input, nullptr);
    jfloat* out = env->GetFloatArrayElements(output, nullptr);
    if (in == nullptr || out == nullptr) {
        return -3;
    }

    const int result = gEngine->processBuffer(in, out, frames);
    env->ReleaseFloatArrayElements(input, in, JNI_ABORT);
    env->ReleaseFloatArrayElements(output, out, 0);
    return result;
}
