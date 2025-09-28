package com.example.test

import android.os.Bundle
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    companion object {
        init {
            System.loadLibrary("test")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        inflateControls()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUi()
        }
    }

    private fun hideSystemUi() {
        val decorView = window.decorView
        decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }

    private fun inflateControls() {
        val root = findViewById<ViewGroup>(android.R.id.content)
        layoutInflater.inflate(R.layout.controller_overlay, root)

        val buttonUp = root.findViewById<Button>(R.id.buttonUp)
        val buttonDown = root.findViewById<Button>(R.id.buttonDown)
        val buttonLeft = root.findViewById<Button>(R.id.buttonLeft)
        val buttonRight = root.findViewById<Button>(R.id.buttonRight)

        buttonUp.setOnClickListener { sendKeyPress(KeyEvent.KEYCODE_DPAD_UP) }
        buttonDown.setOnClickListener { sendKeyPress(KeyEvent.KEYCODE_DPAD_DOWN) }
        buttonLeft.setOnClickListener { sendKeyPress(KeyEvent.KEYCODE_DPAD_LEFT) }
        buttonRight.setOnClickListener { sendKeyPress(KeyEvent.KEYCODE_DPAD_RIGHT) }
    }

    private fun sendKeyPress(keyCode: Int) {
        val eventTime = SystemClock.uptimeMillis()
        dispatchKeyEvent(KeyEvent(eventTime, eventTime, KeyEvent.ACTION_DOWN, keyCode, 0))
        dispatchKeyEvent(KeyEvent(eventTime, eventTime, KeyEvent.ACTION_UP, keyCode, 0))
    }
}
