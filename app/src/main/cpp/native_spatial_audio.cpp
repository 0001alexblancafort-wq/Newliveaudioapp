#include <jni.h>
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#define LOG_TAG "SpatialNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
constexpr float kPi = 3.14159265358979323846f;

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

        historyL_.assign(256, 0.0f);
        historyR_.assign(256, 0.0f);
        LOGI("Spatial engine initialized: sr=%d, channels=%d", sampleRate_, channels_);
        return true;
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        historyL_.clear();
        historyR_.clear();
        sampleRate_ = 48000;
        channels_ = 2;
    }

    void setParameters(float azimuthDeg, float elevationDeg, float proximity) {
        std::lock_guard<std::mutex> lock(mutex_);
        azimuth_ = azimuthDeg;
        elevation_ = std::clamp(elevationDeg, 0.0f, 90.0f);
        proximity_ = std::clamp(proximity, 0.0f, 1.0f);
    }

    int processBuffer(const float* input, float* output, int frames) {
        if (input == nullptr || output == nullptr || frames <= 0) {
            return -1;
        }

        const int stride = channels_ * 2;
        for (int i = 0; i < frames; ++i) {
            const int base = i * stride;
            const float inL = input[base];
            const float inR = input[base + 1];

            const float angle = azimuth_ * kPi / 180.0f;
            const float elevation = elevation_ * kPi / 180.0f;
            const float pan = std::sin(angle) * 0.9f;
            const float frontBack = 0.35f * std::cos(angle);
            const float vertical = 0.25f * std::sin(elevation);

            const float dryGain = 1.0f - proximity_ * 0.6f;
            const float spread = 0.5f + 0.5f * (1.0f - proximity_);
            const float leftGain = (0.7f + pan + frontBack + vertical) * spread * dryGain;
            const float rightGain = (0.7f - pan - frontBack - vertical) * spread * dryGain;
            const float crossGain = 0.12f * (1.0f - proximity_);

            const float left = inL * leftGain + inR * crossGain;
            const float right = inR * rightGain + inL * crossGain;

            output[base] = left;
            output[base + 1] = right;
        }

        return 0;
    }

private:
    std::mutex mutex_;
    int sampleRate_ = 48000;
    int channels_ = 2;
    float azimuth_ = 0.0f;
    float elevation_ = 45.0f;
    float proximity_ = 0.5f;
    std::vector<float> historyL_;
    std::vector<float> historyR_;
};

std::unique_ptr<SpatialAudioEngine> gEngine;
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
