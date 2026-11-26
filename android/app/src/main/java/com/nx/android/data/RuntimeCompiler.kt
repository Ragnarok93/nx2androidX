package com.nx.android.data

import com.nx.android.BuildConfig
import com.nx.android.domain.GameConfig
import com.nx.android.domain.PortEvents
import com.nx.android.data.porting.BuildWorkspaces
import com.nx.android.domain.environment.GuestProcessLauncher
import java.io.File
import javax.inject.Inject

/** The outcome of a runtime update: whether it worked, and the reason it did not. */
data class RuntimeUpdateResult(val success: Boolean, val reason: String = "", val profileDrift: Boolean = false)

data class ApkUpgradeResult(val apk: File, val companion: File?)

/** Executes only rebuild-runtime; a successful process must also report the requested pack. */
class RuntimeCompiler @Inject constructor(private val guestLauncher: GuestProcessLauncher) {
    fun rebuild(pack: String, noLsfg: Boolean = !BuildConfig.NX_LSFG, allowProfileDrift: Boolean = false,
                log: (String) -> Unit = AppLog::line): RuntimeUpdateResult {
        var committed = false
        var reason = ""
        var drift = false
        return try {
            val process = guestLauncher.guest(args(pack, noLsfg, allowProfileDrift))
            process.inputStream.bufferedReader().useLines { lines ->
                lines.forEach { line ->
                    val event = PortEvents.parse(line)
                    if (event?.kind == "result") {
                        committed = File(event.data).canonicalFile == File(pack).canonicalFile
                    }
                    // 档案对不上：玩家还可以让 App 用当前档案再来一次
                    if (event?.kind == "profile-drift") drift = true
                    val message = event?.message?.takeIf { it.isNotBlank() } ?: line
                    log(message)
                    // the porter's own failure line is what the player needs to see: it says why the update stopped
                    if (message.startsWith("[RED] ")) reason = message.removePrefix("[RED] ")
                }
            }
            val code = process.waitFor()
            if (code != 0) log("[RED] Runtime compiler exited with code $code")
            val success = code == 0 && committed
            val why = when {
                success -> ""
                reason.isNotEmpty() -> reason
                code != 0 -> "Runtime compiler exited with code $code"
                else -> "The runtime update did not report this pack"
            }
            RuntimeUpdateResult(success, why, drift)
        } catch (error: Exception) {
            log("[RED] Runtime update failed: ${error.message}")
            RuntimeUpdateResult(false, error.message.orEmpty())
        }
    }

    fun upgradeApk(request: ApkUpgradeRequest, keys: String, firmware: String, config: GameConfig?,
                   workspaces: BuildWorkspaces, upgradeApks: UpgradeApks, log: (String) -> Unit): ApkUpgradeResult {
        val (target, data, game, key, stage, cancellation) = request
        check(upgradeApks.archiveIdentity(target.apk) == target.identity) {
            "The selected APK changed. Choose it again."
        }
        check(target.embedded || (data != null && upgradeApks.matchesCompanion(data, target.companionDigest))) {
            "The selected companion folder does not match the game APK."
        }
        val installed = if (game == null) {
            upgradeApks.requireInstallableVersion(target.identity)
            if (target.apk.length() + (64L shl 20) > android.os.StatFs(stage.parentFile!!.absolutePath).availableBytes) {
                error("Not enough storage to stage the runtime-upgraded APK.")
            }
            null
        } else {
            check(target.identity.versionCode < Int.MAX_VALUE) {
                "The selected port's version code is too high to build a newer standalone APK."
            }
            upgradeApks.installedVersionFor(target.identity)
        }
        val output = if (game == null) File(stage, "${target.identity.packageName}-upgraded.apk") else stage
        val args = apkArgs(target.apk.absolutePath, output.absolutePath,
            data?.absolutePath.orEmpty(), !BuildConfig.NX_LSFG, key?.absolutePath.orEmpty()).toMutableList()
        args += listOf("--cancel-file", cancellation.absolutePath)

        val workspace = game?.let {
            args += listOf("--game-update", it.patch.absolutePath, "--base", it.base.absolutePath,
                "--package", target.identity.packageName, "--version-floor",
                maxOf(target.identity.versionCode + 1, installed ?: 0).toString())
            if (keys.isNotBlank()) args += listOf("--keys", keys)
            if (firmware.isNotBlank()) args += listOf("--firmware", firmware)
            if (config != null) {
                if (config.touch) args += "--touch"
                if (config.fps60) args += "--fps60"
                if (config.mtReplay) args += "--mt-replay"
                if (config.aliasHeadroom >= 0) args += listOf("--alias-headroom", config.aliasHeadroom.toString())
                if (File(config.sharedFont).isFile) args += listOf("--shared-font", config.sharedFont)
            }
            workspaces.prepare(it.base.absolutePath, "game-update:${it.patch.canonicalPath}")
        }
        var code: Int? = null
        try {
            check(!cancellation.exists()) { "APK upgrade cancelled." }
            val process = guestLauncher.guest(args,
                workspace?.let { mapOf("NX2A_WORK" to it.absolutePath) }.orEmpty())
            var reported: File? = null
            var reason = ""
            try {
                process.inputStream.bufferedReader().useLines { lines ->
                    lines.forEach { line ->
                        log(line)
                        if (line.startsWith("/") && line.endsWith(".apk")) reported = File(line)
                        if (line.startsWith("APK upgrade failed:")) reason = line
                    }
                }
                code = process.waitFor()
            } finally {
                if (process.isAlive) {
                    cancellation.createNewFile()
                    process.waitFor()
                }
            }
            check(!cancellation.exists()) { "APK upgrade cancelled." }
            check(code == 0) { reason.ifBlank { "APK upgrade compiler exited with code $code." } }
            val apk = checkNotNull(reported?.canonicalFile?.takeIf {
                it.parentFile == stage.canonicalFile && it.isFile && it.length() > 0
            }) { "No completed upgrade APK was reported." }
            val verified = upgradeApks.inspect(apk)
            upgradeApks.verifyResult(target, verified.identity, game != null)
            val companion = if (game == null) data else {
                apk.resolveSibling("${apk.nameWithoutExtension}-data").takeIf(File::isDirectory)
            }
            check(verified.embedded ||
                (companion != null && upgradeApks.matchesCompanion(companion, verified.companionDigest))) {
                "The completed upgrade is missing its companion game data."
            }
            check(!cancellation.exists()) { "APK upgrade cancelled." }
            return ApkUpgradeResult(apk, companion)
        } finally {
            if (workspace != null && (cancellation.exists() || !PortRunner.isInterruptedExit(code))) {
                workspaces.discard(workspace, null) { log("[RED] $it") }
            }
        }
    }

    companion object {
        fun apkArgs(source: String, output: String, data: String, noLsfg: Boolean, key: String = ""): List<String> {
            val args = mutableListOf("-m", "porter", "upgrade-apk", source, "-o", output)
            if (data.isNotBlank()) args += listOf("--data", data)
            if (key.isNotBlank()) args += listOf("--key", key)
            if (noLsfg) args += "--no-lsfg"
            return args
        }


        fun args(pack: String, noLsfg: Boolean, allowProfileDrift: Boolean = false): List<String> {
            val args = mutableListOf("-m", "porter", "rebuild-runtime", pack, "--progress", "json")
            if (noLsfg) args.add("--no-lsfg")
            // the player asked to rebuild with the profile the game has now
            if (allowProfileDrift) args.add("--allow-profile-drift")
            return args
        }
    }
}
