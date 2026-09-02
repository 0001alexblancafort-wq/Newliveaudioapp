package com.tuapp.spatialaudio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import kotlin.math.PI
import kotlin.math.sin

class AudioPipeline(private val nativeAudio: NativeSpatialAudio) {
    private val sampleRate = 48000
    private val framesPerBuffer = 256
    private var audioTrack: AudioTrack? = null
    private var worker: Thread? = null

    @Synchronized
    fun start() {
        if (worker?.isAlive == true) return

        val minBuffer = AudioTrack.getMinBufferSize(
            sampleRate,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_FLOAT
        )
        if (minBuffer <= 0) return

        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build()
            )
            .setBufferSizeInBytes(maxOf(minBuffer, framesPerBuffer * 2 * Float.SIZE_BYTES * 2))
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        audioTrack = track
        worker = Thread {
            val input = FloatArray(framesPerBuffer * 2)
            val output = FloatArray(framesPerBuffer * 2)
            var frame = 0L
            track.play()
            while (!Thread.currentThread().isInterrupted) {
                for (index in 0 until framesPerBuffer) {
                    val time = frame++ / sampleRate.toFloat()
                    input[index * 2] = sin(2.0 * PI * 440.0 * time).toFloat() * 0.2f
                    input[index * 2 + 1] = sin(2.0 * PI * 330.0 * time).toFloat() * 0.2f
                }
                if (nativeAudio.processBuffer(input, output, framesPerBuffer) != 0) break
                if (track.write(output, 0, output.size, AudioTrack.WRITE_BLOCKING) < 0) break
            }
            track.stop()
            track.release()
        }.also { it.start() }
    }

    @Synchronized
    fun stop() {
        worker?.interrupt()
        worker = null
        audioTrack = null
    }
}