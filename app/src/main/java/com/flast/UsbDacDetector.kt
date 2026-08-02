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

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log

class UsbDacDetector(
    private val context: Context,
    private val listener: Listener
) {
    interface Listener {
        fun onUsbDeviceAttached(vendorId: Int, productId: Int)
        fun onUsbDeviceDetached(vendorId: Int, productId: Int)
    }

    companion object {
        private const val TAG = "FlastUsbDac"
    }

    private var registered = false

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            @Suppress("DEPRECATION")
            val device: UsbDevice = (
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                    intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                else
                    intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                ) ?: return

            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    Log.i(TAG, "USB conectado: VID=${device.vendorId} PID=${device.productId}")
                    listener.onUsbDeviceAttached(device.vendorId, device.productId)
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    Log.i(TAG, "USB desconectado: VID=${device.vendorId} PID=${device.productId}")
                    listener.onUsbDeviceDetached(device.vendorId, device.productId)
                }
            }
        }
    }

    fun register() {
        if (registered) return
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            context.registerReceiver(receiver, filter)
        }
        registered = true
    }

    fun unregister() {
        if (!registered) return
        try {
            context.unregisterReceiver(receiver)
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "unregister: receiver ya no estaba registrado")
        }
        registered = false
    }

    fun currentlyConnectedDevices(): List<UsbDevice> {
        val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        return usbManager.deviceList.values.toList()
    }
}
