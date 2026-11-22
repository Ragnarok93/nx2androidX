package com.nx.android.data

import android.content.Context
import android.os.Build
import android.os.Environment
import android.os.storage.StorageManager
import com.nx.android.R
import java.io.File
import java.io.IOException

/** Shared browsing roots and app-specific directories on removable volumes. */
object StorageVolumes {

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

    /** Shared volume roots for direct-path browsing, including read-only SD cards and USB drives. */
    fun list(ctx: Context, locale: AppLocale): List<Pair<String, File>> {
        val roots = LinkedHashMap<File, String>()
        val internal = Environment.getExternalStorageDirectory().normalizedRoot()
        if (internal.isDirectory) roots[internal] = locale.str(R.string.storage_internal)

        val manager = ctx.getSystemService(StorageManager::class.java)
        for (volume in manager?.storageVolumes.orEmpty()) {
            if (volume.state != Environment.MEDIA_MOUNTED &&
                volume.state != Environment.MEDIA_MOUNTED_READ_ONLY) continue
            val directory = if (Build.VERSION.SDK_INT >= 30) volume.directory else null
            val root = (directory ?: volume.uuid?.let { File("/storage", it) })?.normalizedRoot() ?: continue
            if (root !in roots) {
                roots[root] = if (volume.isPrimary) locale.str(R.string.storage_internal)
                              else volume.getDescription(ctx)
            }
        }

        // Older or vendor-specific implementations can report an app directory
        // without reporting its corresponding StorageVolume.
        for (dir in ctx.getExternalFilesDirs(null)) {
            val path = dir?.path ?: continue
            if (!path.startsWith("/storage/") || "/Android/data/" !in path) continue
            val root = File(path.substringBefore("/Android/data/")).normalizedRoot()
            if (root !in roots && root.isDirectory) {
                val state = runCatching { Environment.getExternalStorageState(dir) }.getOrNull()
                if (state == Environment.MEDIA_MOUNTED || state == Environment.MEDIA_MOUNTED_READ_ONLY) {
                    roots[root] = locale.str(R.string.storage_external, root.name)
                }
            }
        }

        return roots.map { (root, label) -> label to root }
    }

    private fun File.normalizedRoot(): File = try {
        canonicalFile
    } catch (_: IOException) {
        absoluteFile
    }
}
