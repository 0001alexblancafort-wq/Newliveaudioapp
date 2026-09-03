package com.tuapp.spatialaudio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import java.io.File

class AudioCaptureService : Service() {
    private val nativeAudio = NativeAudioSession.nativeAudio
    private var captureThread: Thread? = null
    private var audioRecord: AudioRecord? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            stopSelf()
            return START_NOT_STICKY
        }

        val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, 0)
        val resultData = intent.parcelableIntentExtra<Intent>(EXTRA_RESULT_DATA) ?: run {
            stopSelf()
            return START_NOT_STICKY
        }
        startForeground(NOTIFICATION_ID, notification())
        startCapture(resultCode, resultData, intent.getStringExtra(EXTRA_HRTF_PATH))
        return START_NOT_STICKY
    }

    private fun startCapture(resultCode: Int, resultData: Intent, hrtfPath: String?) {
        if (captureThread?.isAlive == true) return
        nativeAudio.initEngine(SAMPLE_RATE, CHANNELS)
        if (hrtfPath != null && File(hrtfPath).exists()) {
            nativeAudio.loadHrtfFromFile(hrtfPath)
        }
        if (!nativeAudio.startAudio()) {
            stopSelf()
            return
        }

        val projectionManager = getSystemService(MediaProjectionManager::class.java)
        val projection = projectionManager.getMediaProjection(resultCode, resultData)
        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
            .setSampleRate(SAMPLE_RATE)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build()
        val captureConfig = AudioPlaybackCaptureConfiguration.Builder(projection)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .build()
        val minBuffer = AudioRecord.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_STEREO,
            AudioFormat.ENCODING_PCM_FLOAT
        )
        audioRecord = AudioRecord.Builder()
            .setAudioFormat(format)
            .setBufferSizeInBytes(maxOf(minBuffer, FRAME_COUNT * CHANNELS * Float.SIZE_BYTES * 4))
            .setAudioPlaybackCaptureConfig(captureConfig)
            .build()

        captureThread = Thread {
            val samples = FloatArray(FRAME_COUNT * CHANNELS)
            audioRecord?.startRecording()
            while (!Thread.currentThread().isInterrupted) {
                val count = audioRecord?.read(samples, 0, samples.size, AudioRecord.READ_BLOCKING) ?: 0
                if (count > 0) nativeAudio.enqueueAudio(samples.copyOf(count))
            }
        }.also { it.start() }
    }

    override fun onDestroy() {
        captureThread?.interrupt()
        captureThread = null
        audioRecord?.runCatching {
            stop()
            release()
        }
        audioRecord = null
        nativeAudio.stopAudio()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun notification(): Notification = NotificationCompat.Builder(this, CHANNEL_ID)
        .setSmallIcon(android.R.drawable.ic_media_play)
        .setContentTitle("Spatial Audio")
        .setContentText("Procesando audio PCM del dispositivo")
        .setOngoing(true)
        .build()

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            getSystemService(NotificationManager::class.java).createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Spatial Audio", NotificationManager.IMPORTANCE_LOW)
            )
        }
    }

    private inline fun <reified T : android.os.Parcelable> Intent.parcelableIntentExtra(key: String): T? =
        if (Build.VERSION.SDK_INT >= 33) getParcelableExtra(key, T::class.java) else @Suppress("DEPRECATION") getParcelableExtra(key)

    companion object {
        const val EXTRA_RESULT_CODE = "result_code"
        const val EXTRA_RESULT_DATA = "result_data"
        const val EXTRA_HRTF_PATH = "hrtf_path"
        private const val CHANNEL_ID = "spatial_audio_capture"
        private const val NOTIFICATION_ID = 101
        private const val SAMPLE_RATE = 48000
        private const val CHANNELS = 2
        private const val FRAME_COUNT = 256
    }
}
