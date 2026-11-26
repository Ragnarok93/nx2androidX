package com.nx.android.data.porting

import com.nx.android.core.storage.AppPaths
import com.nx.android.data.LogBundle
import java.io.File
import java.io.RandomAccessFile
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.FileVisitResult
import java.nio.file.attribute.BasicFileAttributes
import java.security.MessageDigest
import javax.inject.Inject
import javax.inject.Singleton

/** Owns private standalone build products. The porter publishes its APK and companion data outside this tree. */
@Singleton
class BuildWorkspaces(private val root: File) {
    @Inject constructor(paths: AppPaths) : this(File(paths.work, "standalone"))

    @Synchronized
    fun prepare(source: String): File {
        check(!Files.isSymbolicLink(root.toPath())) { "Invalid standalone workspace root" }
        val workspace = workspaceFor(source)
        check(!Files.isSymbolicLink(workspace.toPath())) { "Invalid standalone workspace" }
        val marker = File(workspace, DISCARD)
        if (Files.exists(marker.toPath(), NOFOLLOW_LINKS)) deleteWorkspace(workspace, marker)
        check(workspace.isDirectory || workspace.mkdirs()) { "Cannot create standalone workspace" }
        return workspace
    }

    /** Record the intent before removing anything, so a later launch can retry a partial cleanup. */
    @Synchronized
    fun discard(workspace: File, diagnostics: File?, onWarning: (String) -> Unit) {
        if (!workspace.isDirectory || Files.isSymbolicLink(workspace.toPath())) return
        val marker = File(workspace, DISCARD)
        // Preserve only bounded text logs. A diagnostics failure must not block reclaiming game data.
        runCatching { marker.createNewFile() }.onFailure { onWarning("Could not record workspace cleanup: ${it.message}") }
        if (diagnostics != null) {
            runCatching { preserveStageLogs(workspace, diagnostics) }
                .onFailure { onWarning("Could not preserve stage logs: ${it.message}") }
        }
        runCatching { deleteWorkspace(workspace, marker) }
            .onFailure { onWarning("Could not completely free the build workspace: ${it.message}") }
    }

    @Synchronized
    fun reconcile(onWarning: (String) -> Unit) {
        if (Files.isSymbolicLink(root.toPath())) return
        for (workspace in root.listFiles().orEmpty()) {
            if (!WORKSPACE_NAME.matches(workspace.name) ||
                !Files.isDirectory(workspace.toPath(), NOFOLLOW_LINKS)) continue
            val marker = File(workspace, DISCARD)
            if (!marker.isFile) continue
            runCatching { deleteWorkspace(workspace, marker) }
                .onFailure { onWarning("Could not retry workspace cleanup: ${it.message}") }
        }
    }

    fun stageLogs(source: String): List<Pair<String, File>> =
        workspaceLogs(workspaceFor(source))

    private fun workspaceFor(source: String): File {
        val file = File(source)
        val path = runCatching { file.canonicalPath }.getOrElse { file.absolutePath }
        val key = MessageDigest.getInstance("SHA-256").digest(path.toByteArray()).joinToString("") { "%02x".format(it) }
        return File(root, key)
    }

    private fun deleteWorkspace(workspace: File, marker: File) {
        Files.walkFileTree(workspace.toPath(), object : SimpleFileVisitor<Path>() {
            override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                if (file != marker.toPath()) Files.delete(file)
                return FileVisitResult.CONTINUE
            }

            override fun postVisitDirectory(dir: Path, error: java.io.IOException?): FileVisitResult {
                if (error != null) throw error
                if (dir == workspace.toPath()) Files.deleteIfExists(marker.toPath())
                Files.delete(dir)
                return FileVisitResult.CONTINUE
            }
        })
    }

    private fun workspaceLogs(workspace: File): List<Pair<String, File>> {
        if (!Files.isDirectory(workspace.toPath(), NOFOLLOW_LINKS)) return emptyList()
        return workspace.listFiles().orEmpty().flatMap { build ->
            val logs = File(build, "logs")
            if (!Files.isDirectory(build.toPath(), NOFOLLOW_LINKS) ||
                !Files.isDirectory(logs.toPath(), NOFOLLOW_LINKS)) return@flatMap emptyList()
            logs.listFiles().orEmpty()
                .filter { Files.isRegularFile(it.toPath(), NOFOLLOW_LINKS) && it.length() > 0L }
                .map { "${build.name}/${it.name}" to it }
        }
    }

    private fun preserveStageLogs(workspace: File, diagnostics: File) {
        for ((name, log) in workspaceLogs(workspace)) {
            val target = File(diagnostics, name)
            target.parentFile?.mkdirs()
            RandomAccessFile(log, "r").use { input ->
                val length = input.length()
                val start = (length - LogBundle.STAGE_LIMIT).coerceAtLeast(0)
                input.seek(start)
                val tail = ByteArray((length - start).toInt())
                input.readFully(tail)
                target.writeBytes(tail)
            }
        }
    }

    private companion object {
        const val DISCARD = ".discard"
        val WORKSPACE_NAME = Regex("[0-9a-f]{64}")
    }
}
