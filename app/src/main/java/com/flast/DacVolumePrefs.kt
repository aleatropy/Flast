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

import android.content.Context
import android.util.Log
import java.io.File

object DacVolumePrefs {
    private const val TAG = "FlastDacPrefs"
    private const val FILENAME = "dac_volume_prefs.txt"

    private fun prefsFile(context: Context): File = File(context.filesDir, FILENAME)

    private fun readAll(context: Context): MutableMap<String, Boolean> {
        val map = mutableMapOf<String, Boolean>()
        val file = prefsFile(context)
        if (!file.exists()) return map
        file.readLines().forEach { line ->
            if (line.isBlank()) return@forEach
            val parts = line.split(":")
            if (parts.size == 3) {
                val key = "${parts[0]}:${parts[1]}"
                map[key] = parts[2] == "SI"
            }
        }
        return map
    }

    private fun writeAll(context: Context, map: Map<String, Boolean>) {
        val text = map.entries.joinToString("\n") { (key, hasControl) ->
            "$key:${if (hasControl) "SI" else "NO"}"
        }
        prefsFile(context).writeText(text)
    }

    fun getSavedAnswer(context: Context, vendorId: Int, productId: Int): Boolean? {
        val answer = readAll(context)["$vendorId:$productId"]
        Log.i(TAG, "getSavedAnswer($vendorId, $productId) = $answer")
        return answer
    }

    fun saveAnswer(context: Context, vendorId: Int, productId: Int, hasOwnVolumeControl: Boolean) {
        val map = readAll(context)
        map["$vendorId:$productId"] = hasOwnVolumeControl
        writeAll(context, map)
        Log.i(TAG, "saveAnswer($vendorId, $productId) = $hasOwnVolumeControl")
    }
}
