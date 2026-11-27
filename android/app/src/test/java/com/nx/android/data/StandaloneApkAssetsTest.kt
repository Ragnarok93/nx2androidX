package com.nx.android.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

class StandaloneApkAssetsTest {
    private val descriptor = """{"folder":"Game-data","manifest_sha256":"${"a".repeat(64)}"}"""

    private fun withApk(vararg entries: Pair<String, String>, block: (ZipFile) -> Unit) {
        val dir = Files.createTempDirectory("upgrade-assets").toFile()
        try {
            val file = File(dir, "game.apk")
            ZipOutputStream(file.outputStream()).use { out ->
                for ((path, value) in entries) {
                    out.putNextEntry(ZipEntry(path))
                    out.write(value.toByteArray(Charsets.UTF_8))
                    out.closeEntry()
                }
            }
            ZipFile(file).use(block)
        } finally {
            dir.deleteRecursively()
        }
    }

    @Test fun readsCompanionMetadataAndBakedPathFromAssetsDirectory() {
        val path = "/storage/emulated/0/Download/nx2android/Game-data"
        withApk("assets/nx2a-companion.json" to descriptor,
            "assets/nx2a-data.txt" to "$path\n") { zip ->
            assertTrue(StandaloneApkAssets.contains(zip, StandaloneApkAssets.COMPANION))
            assertEquals(descriptor, StandaloneApkAssets.readText(zip, StandaloneApkAssets.COMPANION))
            assertEquals(path, StandaloneApkAssets.readText(zip, StandaloneApkAssets.DATA_PATH))
        }
    }

    @Test fun ignoresRootLevelMetadata() {
        withApk("nx2a-companion.json" to "wrong", "assets/nx2a-companion.json" to descriptor) { zip ->
            assertEquals(descriptor, StandaloneApkAssets.readText(zip, StandaloneApkAssets.COMPANION))
        }
        withApk("nx2a-companion.json" to descriptor) { zip ->
            assertFalse(StandaloneApkAssets.contains(zip, StandaloneApkAssets.COMPANION))
            assertNull(StandaloneApkAssets.readText(zip, StandaloneApkAssets.COMPANION))
        }
    }

    @Test fun embeddedGameDoesNotRequireCompanionDescriptor() {
        withApk("assets/nx2a-data.zip" to "fixture") { zip ->
            assertTrue(StandaloneApkAssets.contains(zip, StandaloneApkAssets.EMBEDDED))
            assertNull(StandaloneApkAssets.readText(zip, StandaloneApkAssets.COMPANION))
        }
    }

    @Test fun rejectsEmptyBlankAndOversizedMetadata() {
        for (value in listOf("", " \n", "x".repeat(65537))) {
            withApk("assets/nx2a-companion.json" to value) { zip ->
                assertNull(StandaloneApkAssets.readText(zip, StandaloneApkAssets.COMPANION))
            }
        }
    }
}
