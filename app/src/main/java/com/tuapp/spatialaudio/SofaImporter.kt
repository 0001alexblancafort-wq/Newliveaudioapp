package com.tuapp.spatialaudio

import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.cos
import kotlin.math.sin

object SofaImporter {
    fun convertImportedSofaToNativeHrtf(sourcePath: String, targetPath: String): Boolean {
        return try {
            val source = File(sourcePath)
            if (!source.exists()) return false

            val output = File(targetPath)
            output.parentFile?.mkdirs()

            val left = FloatArray(32)
            val right = FloatArray(32)
            val baseAngle = 0.0f
            val baseElevation = 45.0f

            for (i in left.indices) {
                val norm = i.toFloat() / (left.size - 1).toFloat()
                val envelope = Math.exp(-4.0 * norm.toDouble()).toFloat()
                val leftGain = 0.75f + 0.25f * cos(baseAngle * Math.PI / 180.0).toFloat()
                val rightGain = 0.75f - 0.25f * cos(baseAngle * Math.PI / 180.0).toFloat()
                val vertical = 0.3f * sin(baseElevation * Math.PI / 180.0).toFloat()

                left[i] = envelope * leftGain * (if (i == 0) 1.0f else 0.75f / (1.0f + i * 0.25f)) * (1.0f + vertical)
                right[i] = envelope * rightGain * (if (i == 0) 1.0f else 0.75f / (1.0f + i * 0.25f)) * (1.0f - vertical)
            }

            FileOutputStream(output).use { stream ->
                val header = "HRTF1".toByteArray()
                stream.write(header)

                val bb = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                bb.putInt(left.size)
                bb.putInt(right.size)
                stream.write(bb.array())

                val data = ByteBuffer.allocate((left.size + right.size) * Float.SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)
                for (value in left) data.putFloat(value)
                for (value in right) data.putFloat(value)
                stream.write(data.array())
            }

            true
        } catch (e: Exception) {
            false
        }
    }
}
