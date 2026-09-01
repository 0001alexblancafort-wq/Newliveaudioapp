package com.tuapp.spatialaudio

import android.app.Service
import android.content.Intent
import android.os.IBinder

class MusicFXService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null
}
