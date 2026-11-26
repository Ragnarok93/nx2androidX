package com.nx.android.data

import com.nx.android.data.porting.BuildWorkspaces
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class BuildWorkspacesTest {
    @get:Rule val temp = TemporaryFolder()

    @Test
    fun `discard removes private products and preserves logs and an external companion`() {
        val workspaces = BuildWorkspaces(temp.newFolder("work"))
        val source = temp.newFile("game.nsp")
        val companion = temp.newFolder("game-data")
        val asset = File(companion, "data").apply { writeText("published") }
        val workspace = workspaces.prepare(source.path)
        val build = File(workspace, "0000000000000010_build")
        File(build, "logs/build.log").apply { parentFile!!.mkdirs(); writeText("trace") }
        File(build, "compiled.bin").writeText("private")
        Files.createSymbolicLink(File(build, "RomFS").toPath(), companion.toPath())
        val diagnostics = temp.newFolder("diagnostics")

        workspaces.discard(workspace, diagnostics) { error(it) }

        assertFalse(workspace.exists())
        assertEquals("published", asset.readText())
        assertEquals("trace", File(diagnostics, "${build.name}/build.log").readText())
        assertTrue(source.isFile)
    }

    @Test
    fun `resume keeps incomplete data and a prior cleanup request is retried`() {
        val workspaces = BuildWorkspaces(temp.newFolder("work"))
        val keptSource = temp.newFile("interrupted.nsp")
        val discardedSource = temp.newFile("cancelled.nsp")
        val interrupted = workspaces.prepare(keptSource.path)
        val partial = File(interrupted, "partial").apply { writeText("checkpoint") }
        val discarded = workspaces.prepare(discardedSource.path)
        File(discarded, ".discard").createNewFile()
        File(discarded, "remaining").writeText("left by a killed cleanup")

        workspaces.reconcile { error(it) }

        assertFalse(discarded.exists())
        assertEquals(interrupted, workspaces.prepare(keptSource.path))
        assertEquals("checkpoint", partial.readText())
    }

    @Test
    fun `system interruptions differ from compiler errors`() {
        for (exit in listOf(129, 130, 137, 143)) assertTrue(PortRunner.isInterruptedExit(exit))
        for (exit in listOf(null, 0, 1, 2, 139)) assertFalse(PortRunner.isInterruptedExit(exit))
    }
}
