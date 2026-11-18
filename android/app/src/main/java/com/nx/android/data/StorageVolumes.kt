package com.nx.android.data

import android.content.Context
import android.os.Environment
import com.nx.android.R
import java.io.File

/**
 * 可浏览的存储卷：内部存储在前，SD 卡卷按 `/storage` 下的卷 ID 在后。
 *
 * 枚举 SD 卡卷的通用手法：`getExternalFilesDirs(null)` 的第一个元素是内部卷
 * （`/storage/emulated/0/Android/data/<pkg>/files`），其余元素裁掉 `/Android/data/<pkg>/files`
 * 前缀就是各卷的根。全文件访问在手，java.io 直读畅通。
 */
object StorageVolumes {

    /**
     * Writable APK export locations on mounted removable media. Android 11+ does not
     * guarantee write access to arbitrary SD-card paths, even with all-files access.
     * The app-owned external-files directory is writable without SAF grants.
     */
    fun removableExportDirs(ctx: Context): List<Pair<String, File>> {
        val primary = Environment.getExternalStorageDirectory().absolutePath
        val results = LinkedHashMap<String, File>()
        for (dir in ctx.getExternalFilesDirs(null)) {
            if (dir == null || Environment.getExternalStorageState(dir) != Environment.MEDIA_MOUNTED) continue
            val root = volumeRoot(dir) ?: continue
            if (root == primary || !File(root).isDirectory) continue
            results[root.substringAfterLast('/')] = File(dir, "exports")
        }
        return results.toList()
    }

    /** Pure selection rule, also exercised in JVM tests. Empty selection is automatic. */
    fun selectExportDirectory(source: File, selected: String, cards: List<Pair<String, File>>): File? {
        if (selected.isNotBlank()) {
            return cards.firstOrNull { it.second.absolutePath == selected }?.second
                ?: throw IllegalArgumentException("Selected SD card is not mounted or writable")
        }
        // A removable source cannot use its parent directory as an APK destination: scoped
        // storage permits reading the game but generally forbids creating sibling files.
        val sourcePath = source.canonicalPath
        val match = cards.firstOrNull { (_, export) ->
            val root = volumeRoot(export) ?: return@firstOrNull false
            sourcePath.startsWith("$root/")
        }
        if (match != null) return match.second
        if (sourcePath.startsWith("/storage/") &&
            !sourcePath.startsWith("/storage/emulated/") &&
            !sourcePath.startsWith("/storage/self/")) {
            throw IllegalArgumentException("Source SD card is not available for APK export")
        }
        return null // internal storage: preserve previous behavior, APK beside game
    }

    fun resolveExportDirectory(ctx: Context, source: File, selected: String): File? =
        selectExportDirectory(source, selected, removableExportDirs(ctx))

    private fun volumeRoot(dir: File): String? {
        val path = dir.absolutePath
        val marker = "/Android/data/"
        val at = path.indexOf(marker)
        return if (path.startsWith("/storage/") && at > "/storage/".length) path.substring(0, at) else null
    }

    /**
     * 可移动卷（SD 卡）上 App 自己的端口目录：卷名 → 那个目录
     * （`<卡>/Android/data/<包名>/files/ports`，和私有存储里的那个同构：端口目录下面才是数据包）。
     *
     * 枚举 SD 卡卷的通用手法：`getExternalFilesDirs(null)` 的第一个元素是内部卷
     * （`/storage/emulated/0/Android/data/<pkg>/files`），其余元素裁掉 `/Android/data/<pkg>/files`
     * 前缀就是各卷的根。全文件访问在手，java.io 直读畅通。
     */
    fun removablePortsDirs(ctx: Context): List<Pair<String, File>> {
        val primary = Environment.getExternalStorageDirectory().absolutePath
        val out = LinkedHashMap<String, File>()
        for (dir in ctx.getExternalFilesDirs(null)) {
            val path = dir?.path ?: continue
            if (!path.startsWith("/storage/")) continue
            val root = path.substringBefore("/Android/data")
            if (root == primary) continue          // 内置存储：包放在那儿是 FUSE，比私有目录慢，不作为落点
            if (!File(root).isDirectory) continue  // 卡不在了
            out[root.substringAfterLast('/')] = File(dir, "ports")
        }
        return out.toList()
    }

    /** 卷名 → 卷根目录（内部存储在前） */
    fun list(ctx: Context, locale: AppLocale): List<Pair<String, File>> {
        val out = mutableListOf<Pair<String, File>>()
        val internal = Environment.getExternalStorageDirectory()
        if (internal.isDirectory) out += locale.str(R.string.storage_internal) to internal
        for (dir in ctx.getExternalFilesDirs(null)) {
            val path = dir?.path ?: continue
            if (!path.startsWith("/storage/")) continue
            val root = path.substringBefore("/Android/data")
            if (root == internal.path || out.any { it.second.path == root }) continue
            if (File(root).isDirectory) out += locale.str(R.string.storage_sd_card, root.substringAfterLast('/')) to File(root)
        }
        return out
    }
}
