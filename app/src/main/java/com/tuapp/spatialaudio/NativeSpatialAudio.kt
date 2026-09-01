package com.tuapp.spatialaudio

class NativeSpatialAudio {
    init {
        System.loadLibrary("spatialaudio")
    }

    external fun initEngine(sampleRate: Int, channels: Int): Boolean
    external fun releaseEngine()
    external fun setParameters(azimuth: Float, elevation: Float, proximity: Float)
    external fun loadHrtfFromFile(path: String): Boolean
    external fun processBuffer(input: FloatArray, output: FloatArray, frames: Int): Int
}
