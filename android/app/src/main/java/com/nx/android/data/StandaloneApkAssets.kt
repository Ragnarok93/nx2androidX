package com.nx.android.data

import java.io.DataInputStream
import java.util.zip.ZipFile

/** Read the same APK asset namespace used by the standalone exporter. */
internal object StandaloneApkAssets {
    const val COMPANION = "nx2a-companion.json"
    const val DATA_PATH = "nx2a-data.txt"
    const val EMBEDDED = "nx2a-data.zip"

    fun contains(zip: ZipFile, name: String): Boolean = zip.getEntry("assets/$name") != null

    fun readText(zip: ZipFile, name: String): String? =
        zip.getEntry("assets/$name")?.takeIf { it.size in 1L..65536L }?.let { entry ->
            val bytes = ByteArray(entry.size.toInt())
            DataInputStream(zip.getInputStream(entry)).use { input ->
                input.readFully(bytes)
                check(input.read() == -1) { "Invalid APK asset length." }
            }
            bytes.toString(Charsets.UTF_8).trim().takeIf(String::isNotBlank)
        }
}
