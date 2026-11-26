package com.nx.android.appstate

import org.junit.Assert.*
import org.junit.Test

class RuntimeUpdateStateTest {
    @Test fun `apk upgrade shares the runtime operation lock and preserves result path`() {
        val state = RuntimeUpdateState()
        assertTrue(state.beginApk("/games/FFR.apk", "/games/FFR-data"))
        assertTrue(state.apkUpgrade)
        assertEquals("/games/FFR-upgraded.apk", state.apkOutput)
        assertEquals("/games/FFR-data", state.apkData)
        assertFalse(state.begin("/other", "Other"))
        state.appendApkLog("Rebuilding runtime")
        state.finish(true)
        assertEquals(RuntimeUpdatePhase.SUCCEEDED, state.phase)
        assertEquals(listOf("Rebuilding runtime"), state.apkLog)
        assertTrue(state.begin("/pack", "Pack"))
        assertFalse(state.apkUpgrade)
        assertEquals("", state.apkOutput)
    }

    @Test fun `installed upgrade uses separate writable output and optional recovery key`() {
        val state = RuntimeUpdateState()
        val source = "/data/app/org.nx2a.game/base.apk"
        val output = "/storage/emulated/0/Download/nx2android/org.nx2a.game-upgraded.apk"
        assertFalse(state.beginApk(source, "", source))
        assertTrue(state.beginApk(source, "/games/Game-data", output, "/backup/title.json"))
        assertEquals(source, state.pack)
        assertEquals(output, state.apkOutput)
        assertEquals("/backup/title.json", state.apkKey)
        state.finish(true)
        state.dismiss()
        assertEquals("", state.apkKey)
    }

    @Test fun `interrupted apk upgrade does not open the legacy pack update dialog`() {
        val state = RuntimeUpdateState("apk:/games/FFR.apk")
        assertTrue(state.apkUpgrade)
        assertTrue(state.interrupted)
        assertEquals("/games/FFR.apk", state.pack)
        assertEquals("/games/FFR-upgraded.apk", state.apkOutput)
    }

    @Test fun `duplicate taps and dismissal cannot release an active update`() {
        val state = RuntimeUpdateState()
        assertTrue(state.begin("/pack/Game-data", "Game"))
        assertFalse(state.begin("/pack/Other-data", "Other"))
        state.dismiss()
        assertTrue(state.running)
        assertEquals("/pack/Game-data", state.pack)
        state.finish(true)
        assertEquals(RuntimeUpdatePhase.SUCCEEDED, state.phase)
        state.dismiss()
        assertEquals(RuntimeUpdatePhase.IDLE, state.phase)
    }

    @Test fun `failure keeps the target for retry without a build task`() {
        val state = RuntimeUpdateState()
        state.begin("/pack/Game-data", "Game")
        state.finish(false)
        assertFalse(state.running)
        assertEquals(RuntimeUpdatePhase.FAILED, state.phase)
        assertTrue(state.begin(state.pack, state.name))
        assertTrue(state.running)
        assertFalse(state.interrupted)
    }

    @Test fun `the failure reason reaches the dialog and a retry clears it`() {
        val state = RuntimeUpdateState()
        state.begin("/pack/Game-data", "Game")
        state.finish(false, "The game profile changed since this pack was built.")
        assertEquals("The game profile changed since this pack was built.", state.reason)
        state.begin(state.pack, state.name)
        assertEquals("", state.reason)
        state.dismiss()
        assertEquals("", state.reason)
    }

    @Test fun `a changed profile is remembered and the player can continue with the current profile`() {
        val state = RuntimeUpdateState()
        state.begin("/pack/Game-data", "Game")
        state.finish(false, "The game profile changed since this pack was built.", profileDrift = true)
        assertTrue(state.profileDrift)     // the dialog offers the current profile
        assertFalse(state.allowDrift)
        state.useCurrentProfile()
        assertTrue(state.allowDrift)
        state.begin(state.pack, state.name)   // the update the player asked for
        assertTrue(state.takeDrift())         // that one carries it
        assertFalse(state.takeDrift())        // and only that one
        state.finish(true)
        assertFalse(state.allowDrift)         // the new fingerprint is in the manifest: no second question
    }

    @Test fun `a plain retry does not take the current profile`() {
        val state = RuntimeUpdateState()
        state.begin("/pack/Game-data", "Game")
        state.finish(false, "The game profile changed since this pack was built.", profileDrift = true)
        state.begin(state.pack, state.name)   // the retry, without the player's choice
        assertFalse(state.takeDrift())
        state.finish(false, "The game profile changed since this pack was built.", profileDrift = true)
        assertTrue(state.profileDrift)        // the dialog offers it again
    }

    @Test fun `another failure offers no current profile`() {
        val state = RuntimeUpdateState()
        state.begin("/pack/Game-data", "Game")
        state.finish(false, "The Android compiler is unavailable.")
        assertFalse(state.profileDrift)
        assertFalse(state.allowDrift)
    }

    @Test fun `a surviving pending marker restores a dismissible interruption prompt`() {
        val state = RuntimeUpdateState("/pack/Game-data")
        assertEquals(RuntimeUpdatePhase.FAILED, state.phase)
        assertTrue(state.interrupted)
        assertFalse(state.running)
        state.dismiss()
        assertEquals(RuntimeUpdatePhase.IDLE, state.phase)
        assertEquals("", state.pack)
    }
}
