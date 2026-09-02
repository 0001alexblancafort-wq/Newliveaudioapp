package com.tuapp.spatialaudio

class AudioPipeline(private val nativeAudio: NativeSpatialAudio) {
    private var playing = false

    @Synchronized
    fun start() {
        if (!playing) playing = nativeAudio.startAudio()
    }

    @Synchronized
    fun stop() {
        if (playing) nativeAudio.stopAudio()
        playing = false
    }
}