package com.nx.android.feature.upgrade

import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.*
import org.junit.Test

class InstalledGameApksTest {
    @Test fun recognizesOnlyStandaloneGameLayoutsAndRejectsUnsafeFolders() {
        val dir = Files.createTempDirectory("installed-game-apks").toFile()
        try {
            fun apk(name: String, vararg entries: Pair<String, String>): File =
                File(dir, name).also { file ->
                    ZipOutputStream(file.outputStream()).use { out ->
                        entries.forEach { (key, value) ->
                            out.putNextEntry(ZipEntry(key)); out.write(value.toByteArray()); out.closeEntry()
                        }
                    }
                }
            val host = "lib/arm64-v8a/libdmhost.so" to "fixture"
            val dex = "classes.dex" to "fixture"
            assertNull(InstalledGameApks.inspect(apk("other.apk", dex)))
            assertNull(InstalledGameApks.inspect(apk("host.apk", host, dex)))
            assertEquals(InstalledGameApks.Layout(false, null),
                InstalledGameApks.inspect(apk("embedded.apk", host, dex, "assets/nx2a-data.zip" to "fixture")))
            assertEquals(InstalledGameApks.Layout(true, "Game-data"),
                InstalledGameApks.inspect(apk("companion.apk", host, dex, "assets/nx2a-companion.json" to "{\"folder\":\"Game-data\"}")))
            assertEquals(InstalledGameApks.Layout(true, null),
                InstalledGameApks.inspect(apk("unsafe.apk", host, dex, "assets/nx2a-companion.json" to "{\"folder\":\"../other\"}")))
            assertNull(InstalledGameApks.inspect(apk("oversized.apk", host, dex, "assets/nx2a-companion.json" to "x".repeat(65537))))
        } finally { dir.deleteRecursively() }
    }
}
