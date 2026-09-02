package com.tuapp.spatialaudio

import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import java.io.File
import java.io.FileOutputStream
import kotlin.math.PI
import kotlin.math.sin

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            var azimuth by remember { mutableFloatStateOf(0f) }
            var elevation by remember { mutableFloatStateOf(45f) }
            var proximity by remember { mutableFloatStateOf(0.5f) }
            var isProcessing by remember { mutableStateOf(false) }
            var hrtfLoaded by remember { mutableStateOf(false) }
            var lastL by remember { mutableFloatStateOf(0f) }
            var lastR by remember { mutableFloatStateOf(0f) }
            var importing by remember { mutableStateOf(false) }
            var playing by remember { mutableStateOf(false) }

            val nativeAudio = remember { NativeSpatialAudio() }
            val audioPipeline = remember { AudioPipeline(nativeAudio) }
            val hrtfTarget = File(filesDir, "HRTF.bin")
            val sofaPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
                if (uri == null) return@rememberLauncherForActivityResult
                importing = true
                val input = contentResolver.openInputStream(uri) ?: run {
                    importing = false
                    return@rememberLauncherForActivityResult
                }
                val tempSofa = File(filesDir, "imported.sofa")
                val output = FileOutputStream(tempSofa)
                input.copyTo(output)
                input.close()
                output.close()

                val ok = nativeAudio.convertSofaToHrtf(tempSofa.absolutePath, hrtfTarget.absolutePath, 48000)
                hrtfLoaded = ok
                importing = false
                if (ok) {
                    nativeAudio.loadHrtfFromFile(hrtfTarget.absolutePath)
                }
            }

            DisposableEffect(nativeAudio) {
                nativeAudio.initEngine(48000, 2)
                onDispose {
                    audioPipeline.stop()
                    nativeAudio.releaseEngine()
                }
            }

            LaunchedEffect(azimuth, elevation, proximity) {
                nativeAudio.setParameters(azimuth, elevation, proximity)
            }

            LaunchedEffect(Unit) {
                val ok = nativeAudio.loadHrtfFromFile(hrtfTarget.absolutePath)
                hrtfLoaded = ok
            }

            MaterialTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(24.dp),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = "Spatial Audio Engine",
                            style = MaterialTheme.typography.headlineMedium
                        )

                        Spacer(modifier = Modifier.height(16.dp))
                        Text(text = "Dirección: ${azimuth.toInt()}°")
                        Slider(
                            value = azimuth,
                            onValueChange = { azimuth = it },
                            valueRange = -90f..90f,
                            modifier = Modifier.fillMaxWidth()
                        )

                        Spacer(modifier = Modifier.height(8.dp))
                        Text(text = "Elevación: ${elevation.toInt()}°")
                        Slider(
                            value = elevation,
                            onValueChange = { elevation = it },
                            valueRange = 0f..90f,
                            modifier = Modifier.fillMaxWidth()
                        )

                        Spacer(modifier = Modifier.height(8.dp))
                        Text(text = "Proximidad: ${"%.2f".format(proximity)}")
                        Slider(
                            value = proximity,
                            onValueChange = { proximity = it },
                            valueRange = 0f..1f,
                            modifier = Modifier.fillMaxWidth()
                        )

                        Spacer(modifier = Modifier.height(24.dp))

                        Text(text = if (hrtfLoaded) "HRTF cargado" else "HRTF fallback activo")

                        Button(
                            onClick = {
                                val mimeTypes = arrayOf("application/octet-stream", "application/sofa", "text/plain")
                                sofaPicker.launch(mimeTypes)
                            }
                        ) {
                            Text(if (importing) "Importando..." else "Añadir SOFA")
                        }

                        Spacer(modifier = Modifier.height(12.dp))

                        Button(
                            onClick = {
                                val frames = 256
                                val input = FloatArray(frames * 2)
                                val output = FloatArray(frames * 2)

                                for (i in 0 until frames) {
                                    val sample = i.toFloat() / 48000f
                                    val left = sin((2f * PI * 440f * sample)).toFloat() * 0.3f
                                    val right = sin((2f * PI * 330f * sample)).toFloat() * 0.3f
                                    input[i * 2] = left
                                    input[i * 2 + 1] = right
                                }

                                nativeAudio.processBuffer(input, output, frames)
                                lastL = output[0]
                                lastR = output[1]
                                isProcessing = !isProcessing
                            }
                        ) {
                            Text("Procesar buffer")
                        }

                        Spacer(modifier = Modifier.height(12.dp))

                        Button(
                            onClick = {
                                if (playing) {
                                    audioPipeline.stop()
                                } else {
                                    audioPipeline.start()
                                }
                                playing = !playing
                            }
                        ) {
                            Text(if (playing) "Detener audio" else "Reproducir audio")
                        }

                        Spacer(modifier = Modifier.height(16.dp))
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(16.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(text = "L: ${"%.4f".format(lastL)}")
                            Text(text = "R: ${"%.4f".format(lastR)}")
                        }
                    }
                }
            }
        }
    }
}
