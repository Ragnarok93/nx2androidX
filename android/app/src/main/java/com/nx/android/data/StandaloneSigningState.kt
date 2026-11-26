package com.nx.android.data

import java.io.File
import java.nio.file.Files
import java.util.UUID

/** Preserve signing identities outside the disposable compiler environment.
 * Call under the environment's signing-state lock, before a guest launch or reset. */
object StandaloneSigningState {
    fun migrate(files: File, nxHome: File): File {
        val current = File(files, "standalone-signing-key.json")
        val suffixes = listOf(".json", ".seed-v1", ".titles", ".json.versions")
        val old = suffixes.map { File(nxHome, "signing-key$it") }
        val next = suffixes.map { File(files, "standalone-signing-key$it") }
        val pending = old.zip(next).filter { (src, _) -> src.exists() }
        if (pending.isEmpty()) return current

        if (pending.none { (_, dst) -> dst.exists() }) {
            // Individual renames are atomic on the app's private volume.
            // On interruption the next call resumes the remaining moves.
            pending.forEach { (src, dst) ->
                check(src.renameTo(dst)) { "Cannot preserve standalone signing state" }
            }
        } else {
            // Never replace a different signer or seed. Preserve the whole
            // old family together in a private, separately searchable root.
            val recovery = File(files, "standalone-signing-key.recovery")
            check(recovery.isDirectory || recovery.mkdirs()) { "Cannot prepare signing recovery" }
            val scratch = Files.createTempDirectory(recovery.toPath(), ".pending-").toFile()
            try {
                pending.forEach { (src, _) ->
                    check(src.copyRecursively(File(scratch, src.name), overwrite = false)) {
                        "Cannot copy standalone signing state"
                    }
                }
                val saved = File(recovery, UUID.randomUUID().toString())
                check(scratch.renameTo(saved)) { "Cannot publish signing recovery" }
                // Only remove originals after the complete backup is published.
                pending.forEach { (src, _) ->
                    check(src.deleteRecursively()) { "Cannot finish signing migration; retry before reset" }
                }
            } finally {
                if (scratch.exists()) scratch.deleteRecursively()
            }
        }
        return current
    }
}
