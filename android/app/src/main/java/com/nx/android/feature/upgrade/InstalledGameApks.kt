package com.nx.android.feature.upgrade

import android.content.Context
import android.content.Intent
import android.os.Environment
import org.json.JSONObject
import java.io.File
import java.util.zip.ZipFile

data class InstalledGameApk(val name: String, val packageName: String, val version: String,
                            val source: File, val companion: File?, val output: File, val needsCompanion: Boolean)

/** Package visibility is restricted to launcher activities. Identify ports by their APK contents,
 * not package prefixes, which can be customized. Never read another application's private data. */
object InstalledGameApks {
    private fun visibleApps(context: Context) = context.packageManager.queryIntentActivities(
        Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER), 0)
        .mapNotNull { it.activityInfo?.applicationInfo }.distinctBy { it.packageName }

    // Recompilation must not silently choose a different signer if ZIP inspection fails.
    // Supply paths for visible packages; the porter uses only the matching package.
    fun signingSources(context: Context): Map<String, String> =
        visibleApps(context).associate { it.packageName to it.sourceDir }

    fun discover(context: Context): List<InstalledGameApk> {
        val pm = context.packageManager
        val downloads = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
        return visibleApps(context).mapNotNull { app ->
                runCatching {
                    if (!app.splitSourceDirs.isNullOrEmpty()) return@runCatching null
                    val source = File(app.sourceDir)
                    val layout = inspect(source) ?: return@runCatching null
                    val companion = layout.folder?.let { name ->
                        listOf(File(downloads, name), File(File(downloads, "nx2android"), name))
                            .firstOrNull { File(it, "nx2a-data.json").isFile }
                    }
                    val version = pm.getPackageInfo(app.packageName, 0).versionName.orEmpty()
                    InstalledGameApk(pm.getApplicationLabel(app).toString(), app.packageName, version,
                        source, companion, File(File(downloads, "nx2android"), app.packageName + "-upgraded.apk"),
                        layout.needsCompanion)
                }.getOrNull()
            }.sortedBy { it.name.lowercase() }
    }

    data class Layout(val needsCompanion: Boolean, val folder: String?)

    /** Central-directory inspection only. The embedded game archive is never opened. */
    fun inspect(file: File): Layout? = runCatching {
        ZipFile(file).use { zip ->
            if (zip.getEntry("lib/arm64-v8a/libdmhost.so") == null ||
                zip.getEntry("classes.dex") == null) return@use null
            if (zip.getEntry("assets/nx2a-data.zip") != null) return@use Layout(false, null)
            val entry = zip.getEntry("assets/nx2a-companion.json") ?: return@use null
            if (entry.size !in 1L..65536L) return@use null
            // InputStream.readNBytes is not available on every supported Android API.
            val bytes = ByteArray(entry.size.toInt())
            val exact = java.io.DataInputStream(zip.getInputStream(entry)).use {
                it.readFully(bytes)
                it.read() == -1
            }
            if (!exact) return@use null
            val folder = JSONObject(bytes.toString(Charsets.UTF_8)).optString("folder")
                .takeIf { it.isNotBlank() && it != "." && it != ".." && '/' !in it && '\\' !in it }
            Layout(true, folder)
        }
    }.getOrNull()
}
