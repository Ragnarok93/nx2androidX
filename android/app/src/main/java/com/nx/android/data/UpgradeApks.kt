package com.nx.android.data

import android.content.Context
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import android.os.Environment
import dagger.hilt.android.qualifiers.ApplicationContext
import com.nx.android.feature.upgrade.InstalledGameApks
import java.io.DataInputStream
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile
import org.json.JSONObject
import javax.inject.Inject

data class ApkIdentity(val packageName: String, val versionCode: Long, val certificates: Set<String>)

data class UpgradeTarget(
    val apk: File,
    val identity: ApkIdentity,
    val version: String,
    val label: String,
    val embedded: Boolean,
    val companion: File?,
    val companionDigest: String?,
)

data class GameUpdateInputs(val patch: File, val base: File)

data class ApkUpgradeRequest(
    val target: UpgradeTarget,
    val data: File?,
    val game: GameUpdateInputs?,
    val key: File?,
    val staging: File,
    val cancellation: File,
)

class UpgradeApks @Inject constructor(@ApplicationContext private val context: Context) {
    private val pm = context.packageManager
    @Suppress("DEPRECATION")
    private val signingFlags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
        PackageManager.GET_SIGNING_CERTIFICATES else PackageManager.GET_SIGNATURES
    private val downloads: File
        get() = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
    private val stagingRoot: File
        get() = File(downloads, "nx2android/Upgrades")

    fun newRequest(target: UpgradeTarget, data: File?, game: GameUpdateInputs?, key: File?): ApkUpgradeRequest {
        val id = UUID.randomUUID().toString()
        return ApkUpgradeRequest(target, data, game, key,
            File(stagingRoot, id), File(context.filesDir, "cancel-upgrade-$id"))
    }

    fun removeInterruptedStage(path: String) {
        if (path.isEmpty()) return
        val stage = File(path)
        val id = runCatching { UUID.fromString(stage.name).toString() }.getOrNull()
        if (id != stage.name || stage.parentFile != stagingRoot ||
            stage.canonicalFile.parentFile != stagingRoot.canonicalFile) return
        if (stage.isDirectory) {
            Files.walk(stage.toPath()).use { paths ->
                paths.sorted(Comparator.reverseOrder()).forEach { Files.deleteIfExists(it) }
            }
        }
        File(context.filesDir, "cancel-upgrade-$id").delete()
    }

    fun inspect(apk: File): UpgradeTarget {
        require(apk.isFile && apk.canRead()) { "The selected game APK could not be read." }
        val (embedded, companion, digest) = ZipFile(apk).use { zip ->
            check(zip.getEntry("classes.dex") != null &&
                zip.getEntry("lib/arm64-v8a/libdmhost.so") != null &&
                (zip.getEntry("assets/nx2a-data.zip") != null ||
                    zip.getEntry("assets/nx2a-companion.json") != null)) {
                "This APK is not a supported standalone nx2android game."
            }
            if (zip.getEntry("assets/nx2a-data.zip") != null) Triple(true, null, null)
            else {
                val descriptor = JSONObject(readAsset(zip, "nx2a-companion.json").orEmpty())
                val digest = descriptor.getString("manifest_sha256")
                check(digest.matches(Regex("[a-f0-9]{64}"))) { "Invalid companion game data descriptor." }
                val baked = readAsset(zip, "nx2a-data.txt")
                    ?.takeIf { it.startsWith("/") }?.let(::File)
                val adjacent = apk.resolveSibling("${apk.nameWithoutExtension}-data")
                val folderName = descriptor.optString("folder")
                    .takeIf { it.isNotBlank() && it != "." && it != ".." && '/' !in it && '\\' !in it }
                val known = folderName?.let { listOf(File(downloads, it), File(downloads, "nx2android/$it")) }.orEmpty()
                val folder = (listOfNotNull(adjacent, baked) + known).firstOrNull { matchesCompanion(it, digest) }
                Triple(false, folder, digest)
            }
        }
        val info = archiveInfo(apk)
        val label = info.applicationInfo?.let { app ->
            app.sourceDir = apk.absolutePath
            app.publicSourceDir = apk.absolutePath
            pm.getApplicationLabel(app).toString()
        }.orEmpty().ifBlank { info.packageName }
        return UpgradeTarget(apk, identity(info), info.versionName.orEmpty(), label, embedded, companion, digest)
    }

    fun installedGames(): List<Pair<String, File>> =
        InstalledGameApks.discover(context).map { it.name to it.source }

    private fun readAsset(zip: ZipFile, name: String): String? =
        zip.getEntry(name)?.takeIf { it.size in 1L..65536L }?.let { entry ->
            val bytes = ByteArray(entry.size.toInt())
            DataInputStream(zip.getInputStream(entry)).use { input ->
                input.readFully(bytes)
                check(input.read() == -1) { "Invalid APK asset length." }
            }
            bytes.toString(Charsets.UTF_8).trim()
        }

    fun matchesCompanion(folder: File, expected: String?): Boolean {
        val manifest = File(folder, "nx2a-data.json")
        if (expected == null || manifest.length() !in 1..(1 shl 20)) return false
        return runCatching { sha256(manifest.readBytes()) == expected }.getOrDefault(false)
    }

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    fun archiveIdentity(apk: File): ApkIdentity = identity(archiveInfo(apk))

    fun installedVersionFor(archive: ApkIdentity): Long? {
        val installed = try {
            identity(pm.getPackageInfo(archive.packageName, signingFlags))
        } catch (_: PackageManager.NameNotFoundException) {
            return null
        }
        check(installed.certificates == archive.certificates) {
            "The installed game is signed with a different certificate."
        }
        return installed.versionCode
    }

    fun requireInstallableVersion(archive: ApkIdentity) {
        val installed = installedVersionFor(archive) ?: return
        check(archive.versionCode >= installed) {
            "Android would reject version code ${archive.versionCode}. The installed game uses version code $installed."
        }
    }

    fun verifyResult(target: UpgradeTarget, output: ApkIdentity, gameUpdate: Boolean) {
        check(output.packageName == target.identity.packageName) { "The upgraded APK package ID changed." }
        check(output.certificates == target.identity.certificates) { "The upgraded APK signing certificate changed." }
        check(if (gameUpdate) output.versionCode > target.identity.versionCode
              else output.versionCode == target.identity.versionCode) {
            "The upgraded APK has an unexpected version code."
        }
        requireInstallableVersion(output)
    }

    private fun archiveInfo(apk: File): PackageInfo =
        pm.getPackageArchiveInfo(apk.absolutePath, signingFlags)
            ?: error("Android could not read the APK package or signing certificate.")

    @Suppress("DEPRECATION")
    private fun identity(info: PackageInfo): ApkIdentity {
        val currentApi = Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
        val signers = if (currentApi) info.signingInfo?.apkContentsSigners else info.signatures
        check(!signers.isNullOrEmpty()) { "The APK has no readable signing certificate." }
        val version = if (currentApi) info.longVersionCode else info.versionCode.toLong()
        return ApkIdentity(info.packageName, version, signers.map { sha256(it.toByteArray()) }.toSet())
    }
}
