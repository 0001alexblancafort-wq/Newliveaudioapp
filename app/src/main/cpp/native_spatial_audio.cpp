#include <jni.h>
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

#if SPATIAL_USE_SOFA
#include <mysofa.h>
#endif

#define LOG_TAG "SpatialNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kFIRSize = 32;

struct HrtfProfile {
    bool loaded = false;
    std::vector<float> left;
    std::vector<float> right;
    float azimuth = 0.0f;
    float elevation = 0.0f;
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
        sampleRate_ = 48000;
        channels_ = 2;
    }

    void setParameters(float azimuthDeg, float elevationDeg, float proximity) {
        std::lock_guard<std::mutex> lock(mutex_);
        azimuth_ = azimuthDeg;
        elevation_ = std::clamp(elevationDeg, 0.0f, 90.0f);
        proximity_ = std::clamp(proximity, 0.0f, 1.0f);
        updateProfileFromParameters();
    }

    bool loadHrtfFromFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
#if SPATIAL_USE_SOFA
        LOGI("Loading SOFA/HRIR data from %s", path.c_str());
        // En una integración completa, aquí se leería el SOFA con libmysofa y se convertiría a HRTF FIR.
        // Este proyecto deja la rama preparada para la librería real; por ahora se usa la base de respaldo.
        (void)path;
        profile_.loaded = true;
        generateFallbackProfile();
        return true;
#else
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOGE("HRTF file not found: %s", path.c_str());
            generateFallbackProfile();
            return false;
        }

        const std::string magicHeader = "HRTF1";
        char header[5] = {0};
        file.read(header, 5);
        if (file.gcount() != 5 || std::string(header) != magicHeader) {
            LOGE("Invalid HRTF header in %s", path.c_str());
            generateFallbackProfile();
            return false;
        }

        std::uint32_t leftCount = 0;
        std::uint32_t rightCount = 0;
        file.read(reinterpret_cast<char*>(&leftCount), sizeof(leftCount));
        file.read(reinterpret_cast<char*>(&rightCount), sizeof(rightCount));

        profile_.left.resize(leftCount, 0.0f);
        profile_.right.resize(rightCount, 0.0f);
        file.read(reinterpret_cast<char*>(profile_.left.data()), static_cast<std::streamsize>(leftCount * sizeof(float)));
        file.read(reinterpret_cast<char*>(profile_.right.data()), static_cast<std::streamsize>(rightCount * sizeof(float)));

        profile_.loaded = true;
        profile_.azimuth = azimuth_;
        profile_.elevation = elevation_;

        LOGI("HRTF loaded from %s (%zu left taps, %zu right taps)", path.c_str(), profile_.left.size(), profile_.right.size());
        return true;
#endif
    }

    int processBuffer(const float* input, float* output, int frames) {
        if (input == nullptr || output == nullptr || frames <= 0) {
            return -1;
        }

        const int stride = channels_ * 2;
        std::vector<float> leftHistory(kFIRSize, 0.0f);
        std::vector<float> rightHistory(kFIRSize, 0.0f);

        for (int i = 0; i < frames; ++i) {
            const int base = i * stride;
            const float inL = input[base];
            const float inR = input[base + 1];

            const float leftSample = processSample(inL, inR, true, leftHistory);
            const float rightSample = processSample(inR, inL, false, rightHistory);

            output[base] = leftSample;
            output[base + 1] = rightSample;
        }

        return 0;
    }

private:
    void generateFallbackProfile() {
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
        if (!profile_.loaded) {
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
    HrtfProfile profile_;
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
