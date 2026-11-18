package com.nx.android.data

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

class ExternalApkDestinationTest {
    private val external = File("/storage/ABCD-1234/Android/data/com.nx.android/files/exports")
    private val cards = listOf("ABCD-1234" to external)

    @Test fun sourceOnSdCardUsesAppWritableVolume() {
        assertEquals(external, StorageVolumes.selectExportDirectory(
            File("/storage/ABCD-1234/Games/game.nsp"), "", cards))
    }

    @Test fun internalSourceKeepsOriginalOutputBehavior() {
        assertNull(StorageVolumes.selectExportDirectory(
            File("/storage/emulated/0/Download/game.nsp"), "", cards))
    }

    @Test fun explicitCardWorksEvenForInternalSource() {
        assertEquals(external, StorageVolumes.selectExportDirectory(
            File("/storage/emulated/0/Download/game.nsp"), external.absolutePath, cards))
    }

    @Test fun unavailableCardSelectionDoesNotSilentlyFallBack() {
        assertThrows(IllegalArgumentException::class.java) {
            StorageVolumes.selectExportDirectory(
                File("/storage/emulated/0/Download/game.nsp"),
                "/storage/FFFF-FFFF/Android/data/com.nx.android/files/exports", cards)
        }
    }

    @Test fun unmountedSourceCardDoesNotTryUnwritableRoot() {
        assertThrows(IllegalArgumentException::class.java) {
            StorageVolumes.selectExportDirectory(File("/storage/FFFF-FFFF/Games/game.nsp"), "", cards)
        }
    }
}
