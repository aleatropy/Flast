/*
 * Flast — a bit-perfect FLAC player for Android.
 * Copyright (C) 2026 Aleatropy and the Flast contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Bundles libFLAC (decoder only), BSD-3-Clause, (C) Xiph.Org Foundation —
 * see app/src/main/cpp/third_party/flac-1.4.3/COPYING.Xiph
 */

package com.flast

import android.Manifest
import android.app.NativeActivity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.storage.StorageManager
import android.view.KeyEvent
import android.util.Log

class FlastNativeActivity : NativeActivity() {

    companion object {
        private const val TAG = "FlastNativeActivity"
        private const val PERMISSIONS_REQUEST_CODE = 4001

        // Which DAC the PROCESS is currently on. Deliberately static
        // rather than per-Activity: the Activity is destroyed and rebuilt
        // on every back-press and swipe-away while playback keeps running,
        // and onCreate() then re-runs handleDacAttached() for a DAC that
        // was never actually unplugged. With this state on the Activity,
        // that looked like a brand-new device and paused the music every
        // time you reopened the app.
        @Volatile private var processDacVid: Int? = null
        @Volatile private var processDacPid: Int? = null

        init {

            System.loadLibrary("flacplayer_native")
        }
    }

    private external fun jniSetDacBlocked(blocked: Boolean)
    private external fun jniPauseForDeviceChange()
    private external fun jniRescanMusic()
    private external fun jniSetDacQuestionPending(pending: Boolean)
    private external fun jniSetStoragePermission(granted: Boolean)

    private lateinit var audioManager: AudioManager
    private lateinit var usbDacDetector: UsbDacDetector
    private val handler = Handler(Looper.getMainLooper())

    private var usbDacConnected = false
    private var currentDacVid: Int? = null
    private var currentDacPid: Int? = null
    private var currentDacHasOwnVolumeControl: Boolean? = null
    private var bluetoothAudioActive = false

    private val audioDeviceCallback = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) {
            refreshBluetoothState()
        }
        override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
            refreshBluetoothState()
        }
    }

    private fun neededStoragePermission(): String =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) Manifest.permission.READ_MEDIA_AUDIO
        else Manifest.permission.READ_EXTERNAL_STORAGE

    private fun hasStoragePermission(): Boolean =
        checkSelfPermission(neededStoragePermission()) == PackageManager.PERMISSION_GRANTED

    private fun hasNotificationPermission(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
        else true

    private fun requestMissingPermissions() {
        val missing = mutableListOf<String>()
        if (!hasStoragePermission()) missing.add(neededStoragePermission())
        if (!hasNotificationPermission()) missing.add(Manifest.permission.POST_NOTIFICATIONS)
        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), PERMISSIONS_REQUEST_CODE)
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<out String>, grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSIONS_REQUEST_CODE) {
            jniSetStoragePermission(hasStoragePermission())
            if (!hasStoragePermission()) {
                Log.w(TAG, "Sin permiso de almacenamiento -- MUSICA no encontrara nada hasta concederlo.")
            } else {
                // The very first scan can race this permission dialog and
                // run before the user answers it, caching an empty result
                // with no permission to read anything. Now that we know
                // for sure the permission is granted, force a real rescan.
                Log.i(TAG, "Permiso de almacenamiento concedido -- re-escaneando musica.")
                jniRescanMusic()
            }
        }
    }

    private fun startPlaybackService() {
        val intent = Intent(this, PlaybackService::class.java).apply {
            putExtra(PlaybackService.EXTRA_TRACK_NAME, "flast")
            putExtra(PlaybackService.EXTRA_IS_PLAYING, false)
        }
        startForegroundService(intent)
    }

    private val usbListener = object : UsbDacDetector.Listener {
        override fun onUsbDeviceAttached(vendorId: Int, productId: Int) {
            handler.post { handleDacAttached(vendorId, productId) }
        }
        override fun onUsbDeviceDetached(vendorId: Int, productId: Int) {
            handler.post { handleDacDetached(vendorId, productId) }
        }
    }

    private fun handleDacAttached(vendorId: Int, productId: Int) {
        // Pause ONLY for a genuine device change. Reopening the app with
        // the same DAC still plugged in is not a device change, and must
        // not interrupt playback.
        val isNewDevice = vendorId != processDacVid || productId != processDacPid
        if (isNewDevice) {
            // The audio device really did change, which kills whatever
            // stream was open. Pause, keeping the position, so the
            // transport controls stay meaningful and the user resumes
            // exactly where they were.
            jniPauseForDeviceChange()
            Log.i(TAG, "new audio device ($vendorId:$productId) -- paused, position preserved")
        }
        processDacVid = vendorId
        processDacPid = productId
        usbDacConnected = true
        currentDacVid = vendorId
        currentDacPid = productId
        val saved = DacVolumePrefs.getSavedAnswer(this, vendorId, productId)
        if (saved == null) {
            // Blocks playback (and now also pauses it, see
            // pc_set_dac_blocked) and raises the native question screen.
            // This used to put up an AlertDialog, which crashed with
            // BadTokenException if the Activity had already been
            // destroyed while playback continued in the service.
            jniSetDacBlocked(true)
            jniSetDacQuestionPending(true)
        } else {
            currentDacHasOwnVolumeControl = saved
            jniSetDacBlocked(false)
            applyVolumeControlDecision(saved)
        }
    }

    private fun handleDacDetached(vendorId: Int, productId: Int) {
        if (vendorId != currentDacVid || productId != currentDacPid) return

        // Pause, do not stop. System volume may be pinned to 100% because
        // this DAC was doing the attenuating, and that same 100% stream
        // would now land on the phone's own output with nothing in the way
        // -- so silence is mandatory. But silence via pause keeps the track
        // and its timestamp, so the user presses play and carries on.
        jniPauseForDeviceChange()

        processDacVid = null
        processDacPid = null
        usbDacConnected = false
        currentDacVid = null
        currentDacPid = null
        currentDacHasOwnVolumeControl = null
        jniSetDacQuestionPending(false)
        jniSetDacBlocked(false)
        Log.i(TAG, "DAC removed -- paused, position preserved")
    }

    // Called FROM the native DAC question screen when the user taps
    // YES or NO. Native owns the pixels; Kotlin still owns the decision,
    // because persisting it and pinning STREAM_MUSIC both need framework
    // APIs with no NDK equivalent (spec 3.0).
    //
    // Runs on the native UI thread, not the main thread. Both calls below
    // are safe there: DacVolumePrefs is plain file I/O, and
    // AudioManager.setStreamVolume has no looper affinity.
    fun jniAnswerDacVolume(hasOwnControl: Boolean) {
        val vid = currentDacVid
        val pid = currentDacPid
        if (vid != null && pid != null) {
            DacVolumePrefs.saveAnswer(this, vid, pid, hasOwnControl)
        }
        currentDacHasOwnVolumeControl = hasOwnControl
        jniSetDacBlocked(false)
        applyVolumeControlDecision(hasOwnControl)
        Log.i(TAG, "DAC volume answered: hasOwnControl=$hasOwnControl")
    }

    private fun applyVolumeControlDecision(hasOwnControl: Boolean) {
        if (hasOwnControl) {
            val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
            audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, max, 0)
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (currentDacHasOwnVolumeControl == true &&
            (keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN)
        ) {

            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    private fun refreshBluetoothState() {
        val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        // TYPE_BLE_HEADSET / TYPE_BLE_SPEAKER only exist from API 31.
        // They are plain ints so they would compile and simply never
        // match on older releases, but referencing them unguarded below
        // minSdk is the kind of thing that is correct today and a
        // NoSuchFieldError after some future refactor.
        val bleTypes = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            setOf(AudioDeviceInfo.TYPE_BLE_HEADSET, AudioDeviceInfo.TYPE_BLE_SPEAKER)
        else emptySet()
        bluetoothAudioActive = devices.any {
            it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP || it.type in bleTypes
        }
    }

    fun jniGetBitPerfectStateForNative(isBitPerfectNative: Boolean): Int {
        if (bluetoothAudioActive) return 2
        if (!usbDacConnected) return 2
        if (!isBitPerfectNative) return 2
        val ownControl = currentDacHasOwnVolumeControl == true
        val vol = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
        val maxVol = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        return if (ownControl || vol >= maxVol) 0 else 1
    }

    fun jniGetBitPerfectReason(isBitPerfectNative: Boolean): String {
        return when {
            bluetoothAudioActive ->
                "Bluetooth active -- Bluetooth audio is never bit-perfect " +
                    "(lossy codec and/or mandatory resampling)."
            !usbDacConnected ->
                "No USB DAC connected -- playing through the phone's internal " +
                    "output, no bit-perfect path possible."
            !isBitPerfectNative ->
                "AAudio couldn't open the stream in EXCLUSIVE mode with this DAC -- " +
                    "the HAL vendor may be blocking that mode (see section 5)."
            currentDacHasOwnVolumeControl != true &&
                audioManager.getStreamVolume(AudioManager.STREAM_MUSIC) <
                audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC) ->
                "System software volume -- this DAC has no own volume control " +
                    "configured, so volume is adjusted digitally before " +
                    "reaching the DAC. Raise the volume to max or set up the DAC in CONFIG."
            else ->
                "AAudio EXCLUSIVE active and no software attenuation -- the " +
                    "PCM signal reaches the DAC intact."
        }
    }

    // The music library and playlists live entirely in C now
    // (music_library.c, POSIX opendir/readdir per spec section 3.1).
    // The eight String[]-marshalling methods that used to be here were
    // the app's single biggest ART-heap consumer: every one of them
    // built a Kotlin List<String>, copied it to an Array<String>, and
    // had the native side copy it again, so a full library existed four
    // times over at once.
    //
    // This is the one thing left that C cannot get for itself reliably:
    // ANativeActivity::internalDataPath is NULL on some ROMs, and
    // without it there is nowhere to keep the scan cache or playlists.
    // Called at most once per process, and only on that fallback path.
    fun jniFilesDir(): String = filesDir.absolutePath

    // Which storage volumes exist. The C scanner cannot work this out on
    // its own: /storage is drwx--x--x on a real device, so an app can
    // traverse into a path it already knows but cannot LIST the
    // directory to discover one. That made a removable SD card
    // completely invisible — measured on this phone, 658 .flac files on
    // /storage/4818-D416 that a pure-C scan found none of.
    //
    // Both sources are queried and the duplicates are dropped in C by
    // (st_dev, st_ino):
    //   - StorageManager.storageVolumes.directory is the clean answer,
    //     but getDirectory() only exists from API 30.
    //   - getExternalFilesDirs() works all the way back to the minSdk
    //     and yields <volume>/Android/data/<pkg>/files, whose volume
    //     root is the first four path segments removed.
    fun jniStorageRoots(): Array<String> {
        val roots = LinkedHashSet<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val sm = getSystemService(Context.STORAGE_SERVICE) as StorageManager
            for (v in sm.storageVolumes) {
                v.directory?.absolutePath?.let { roots.add(it) }
            }
        }

        for (dir in getExternalFilesDirs(null)) {
            val path = dir?.absolutePath ?: continue
            val marker = "/Android/data/"
            val cut = path.indexOf(marker)
            if (cut > 0) roots.add(path.substring(0, cut))
        }

        Log.i(TAG, "jniStorageRoots: ${roots.joinToString()}")
        return roots.toTypedArray()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager

        jniSetStoragePermission(hasStoragePermission())
        requestMissingPermissions()
        startPlaybackService()

        usbDacDetector = UsbDacDetector(this, usbListener)
        usbDacDetector.register()

        usbDacDetector.currentlyConnectedDevices().firstOrNull()?.let {
            handleDacAttached(it.vendorId, it.productId)
        }

        audioManager.registerAudioDeviceCallback(audioDeviceCallback, handler)
        refreshBluetoothState()
    }

    override fun onDestroy() {
        super.onDestroy()
        usbDacDetector.unregister()
        audioManager.unregisterAudioDeviceCallback(audioDeviceCallback)
        // Intentionally NOT calling stopService() here: PlaybackService is
        // a real foreground service and playback (decode thread + AAudio
        // callback) runs independently of this Activity's native thread.
        // Stopping it on every back-press/swipe-close silenced music that
        // should keep playing in the background. The service is only
        // stopped by an explicit user action (e.g. a stop control), not
        // as a side effect of closing the UI.
    }
}
