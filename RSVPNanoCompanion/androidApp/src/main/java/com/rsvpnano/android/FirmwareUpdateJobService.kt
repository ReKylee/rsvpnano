package com.rsvpnano.android

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.job.JobInfo
import android.app.job.JobParameters
import android.app.job.JobScheduler
import android.app.job.JobService
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import com.rsvpnano.app.createAndroidFirmwareUpdates
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

class FirmwareUpdateJobService : JobService() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null

    override fun onStartJob(params: JobParameters): Boolean {
        job = scope.launch {
            runCatching {
                val updates = createAndroidFirmwareUpdates(filesDir)
                val update = updates.pendingNotification() ?: return@runCatching
                if (postNotification(update.availableVersion)) {
                    updates.markNotified(update.availableVersion)
                }
            }
            jobFinished(params, false)
        }
        return true
    }

    override fun onStopJob(params: JobParameters): Boolean {
        job?.cancel()
        return true
    }

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }

    private fun postNotification(version: String): Boolean {
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            return false
        }
        val notifications = getSystemService(NotificationManager::class.java)
        notifications.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                "Firmware updates",
                NotificationManager.IMPORTANCE_DEFAULT,
            )
        )
        val intent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val notification = Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_download_done)
            .setContentTitle("RSVP Nano update available")
            .setContentText("Firmware $version is ready for your Nano.")
            .setContentIntent(intent)
            .setAutoCancel(true)
            .build()
        notifications.notify(NOTIFICATION_ID, notification)
        return true
    }

    companion object {
        private const val PERIODIC_JOB_ID = 0x52535650
        private const val IMMEDIATE_JOB_ID = PERIODIC_JOB_ID + 1
        private const val NOTIFICATION_ID = 0x5253
        private const val CHANNEL_ID = "firmware_updates"
        private const val DAY_MILLIS = 24L * 60L * 60L * 1000L

        fun configure(context: Context, enabled: Boolean) {
            val scheduler = context.getSystemService(JobScheduler::class.java)
            if (!enabled) {
                scheduler.cancel(PERIODIC_JOB_ID)
                scheduler.cancel(IMMEDIATE_JOB_ID)
                return
            }
            scheduler.schedule(
                JobInfo.Builder(PERIODIC_JOB_ID, ComponentName(context, FirmwareUpdateJobService::class.java))
                    .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
                    .setPeriodic(DAY_MILLIS)
                    .setPersisted(true)
                    .build()
            )
        }

        fun checkNow(context: Context) {
            context.getSystemService(JobScheduler::class.java).schedule(
                JobInfo.Builder(IMMEDIATE_JOB_ID, ComponentName(context, FirmwareUpdateJobService::class.java))
                    .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
                    .build()
            )
        }
    }
}
