package com.dfteam.asthmaguard

import android.Manifest
import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.telephony.SmsManager
import androidx.core.content.ContextCompat
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.util.concurrent.atomic.AtomicInteger

/**
 * Emergency SMS through Android's SmsManager (docs/PLAN.md 6.1: "a platform
 * channel to SmsManager"). Channel "asthmaguard/sms", method "send"
 * {phone, text} -> true once every part of the message reports RESULT_OK.
 */
class MainActivity : FlutterActivity() {
    private val seq = AtomicInteger(0)

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "asthmaguard/sms").setMethodCallHandler { call, result ->
            when (call.method) {
                "send" -> {
                    val phone = call.argument<String>("phone")
                    val text = call.argument<String>("text")
                    if (phone.isNullOrBlank() || text.isNullOrEmpty()) {
                        result.error("args", "phone and text are required", null)
                    } else {
                        send(phone, text, result)
                    }
                }
                else -> result.notImplemented()
            }
        }
    }

    private fun smsManager(): SmsManager =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            applicationContext.getSystemService(SmsManager::class.java)
        } else {
            @Suppress("DEPRECATION")
            SmsManager.getDefault()
        }

    private fun send(phone: String, text: String, result: MethodChannel.Result) {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.SEND_SMS) != PackageManager.PERMISSION_GRANTED) {
            result.success(false)
            return
        }
        val sms = smsManager()
        val parts = sms.divideMessage(text)
        val action = "com.dfteam.asthmaguard.SMS_SENT_" + seq.incrementAndGet()
        val pending = AtomicInteger(parts.size)
        var ok = true
        var answered = false
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                if (resultCode != Activity.RESULT_OK) ok = false
                if (pending.decrementAndGet() == 0 && !answered) {
                    answered = true
                    try { applicationContext.unregisterReceiver(this) } catch (_: Exception) {}
                    result.success(ok)
                }
            }
        }
        ContextCompat.registerReceiver(applicationContext, receiver, IntentFilter(action), ContextCompat.RECEIVER_NOT_EXPORTED)
        val flags = PendingIntent.FLAG_ONE_SHOT or PendingIntent.FLAG_IMMUTABLE
        val sent = ArrayList<PendingIntent>()
        for (i in parts.indices) {
            val intent = Intent(action).setPackage(packageName)
            sent.add(PendingIntent.getBroadcast(applicationContext, seq.incrementAndGet(), intent, flags))
        }
        try {
            sms.sendMultipartTextMessage(phone, null, parts, sent, null)
        } catch (e: Exception) {
            if (!answered) {
                answered = true
                try { applicationContext.unregisterReceiver(receiver) } catch (_: Exception) {}
                result.success(false)
            }
        }
    }
}
