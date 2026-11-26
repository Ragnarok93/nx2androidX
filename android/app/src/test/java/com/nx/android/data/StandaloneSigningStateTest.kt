package com.nx.android.data

import java.io.File
import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class StandaloneSigningStateTest {
    @Test fun preservesCompleteLegacySigningStateBeforeEnvironmentReset() {
        val files = Files.createTempDirectory("signing-migration").toFile()
        try {
            val home = File(files, "nx2a").apply { mkdirs() }
            val entries = mapOf(
                "signing-key.json" to "legacy key",
                "signing-key.seed-v1" to "private seed",
                "signing-key.titles/0100000000010000.json" to "title key",
                "signing-key.titles/0100000000010000.signer-v1" to "certificate pin",
                "signing-key.json.versions/package" to "42",
                "signing-key.titles/0100000000010000.json.versions/package" to "43")
            entries.forEach { (name, value) ->
                File(home, name).apply { parentFile!!.mkdirs(); writeText(value) }
            }
            val current = StandaloneSigningState.migrate(files, home)
            assertEquals(File(files, "standalone-signing-key.json"), current)
            home.deleteRecursively()
            entries.forEach { (name, value) ->
                assertEquals(value, File(files, name.replaceFirst("signing-key", "standalone-signing-key")).readText())
            }
            assertEquals(current, StandaloneSigningState.migrate(files, home))
        } finally { files.deleteRecursively() }
    }

    @Test fun conflictingFamiliesAreArchivedWithoutOverwritingEitherKey() {
        val files = Files.createTempDirectory("signing-conflict").toFile()
        try {
            val home = File(files, "nx2a").apply { mkdirs() }
            val current = File(files, "standalone-signing-key.seed-v1").apply { writeText("current") }
            File(home, "signing-key.seed-v1").writeText("original")
            val title = File(home, "signing-key.titles/0100000000010000.json")
            title.parentFile!!.mkdirs(); title.writeText("original key")
            StandaloneSigningState.migrate(files, home)
            assertEquals("current", current.readText())
            val saved = File(files, "standalone-signing-key.recovery").listFiles()!!.single()
            assertEquals("original", File(saved, "signing-key.seed-v1").readText())
            assertEquals("original key", File(saved, "signing-key.titles/0100000000010000.json").readText())
            assertFalse(title.exists())
            StandaloneSigningState.migrate(files, home)
            assertEquals(1, saved.parentFile!!.listFiles()!!.size)
        } finally { files.deleteRecursively() }
    }
}
