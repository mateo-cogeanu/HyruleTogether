package info.cemu.cemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.core.app.ApplicationProvider
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import android.app.Application
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import kotlinx.coroutines.runBlocking
import java.io.File
import java.io.IOException
import java.util.UUID

@RunWith(AndroidJUnit4::class)
class HyruleSessionTest {
    @get:Rule val compose = createAndroidComposeRule<HyruleLauncherActivity>()

    @Test fun packagedClientLoadsWithoutStartingSession() { System.loadLibrary("MilkBarClient") }

    @Test fun desktopLauncherPagesAndSetupCheckWork() {
        compose.onNodeWithText("Lobby Browser").assertIsDisplayed()
        compose.onNodeWithContentDescription("Previous page").performClick()
        compose.onNodeWithText("BROWSE — ADD BOTW").assertExists()
        compose.onNodeWithText("◆  RUN SETUP CHECK  ◆").performScrollTo().performClick()
        compose.onNodeWithText("Add your BOTW base game in Settings.", substring = true).assertIsDisplayed()
        compose.onNodeWithText("OK").performClick()
        compose.onNodeWithContentDescription("Next page").performClick()
        compose.onNodeWithContentDescription("Next page").performClick()
        compose.onNodeWithText("Selected multiplayer model").assertIsDisplayed()
    }
    @Test fun serverValidationAndPersistence() {
        compose.onNodeWithText("Add Server").performClick()
        compose.onNodeWithText("SAVE").performClick()
        compose.onNodeWithText("Enter a server name, valid address, and port from 1 to 65535.").assertExists()
        compose.onNodeWithText("Server name").performTextInput("Test Hyrule")
        compose.onNodeWithText("Server IP or hostname").performTextInput("127.0.0.1")
        compose.onNodeWithText("SAVE").performClick()
        compose.onAllNodesWithText("Test Hyrule")[0].performScrollTo().assertIsDisplayed()
        compose.activityRule.scenario.recreate()
        compose.onAllNodesWithText("Test Hyrule")[0].performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("Remove").performScrollTo().performClick()
    }
    @Test fun jniRejectsInvalidConnectionWithoutStartingWorkers() {
        assertFalse(HyruleSession.start("", "", 0, "", ""))
        assertEquals("Invalid server or player settings", HyruleSession.status())
        assertFalse(HyruleSession.start("/unused", "host;injected", 5050, "Android", ""))
        assertFalse(HyruleSession.start("/unused", "127.0.0.1", 65536, "Android", ""))
        assertFalse(HyruleSession.start("/unused", "127.0.0.1", 5050, "[Android", ""))
        assertFalse(HyruleSession.start("/unused", "127.0.0.1", 5050, "Android", "", "1", "bad;model"))
    }
    @Test fun unrecognizedGameDoesNotPassBotwGate() { assertFalse(HyruleSession.isBotw("/does-not-exist/U-King.rpx")) }

    private fun xml(id: String, version: Int) = "<menu><title_id>$id</title_id><title_version>$version</title_version></menu>"
    @Test fun metadataRejectsWrongTitlesRegionsAndUpdates() {
        fun parse(id: String, version: Int, kind: String, base: String? = null) =
            xml(id, version).byteInputStream().use { HyruleInstaller.metadata(it, kind, base) }
        assertEquals("0005000e101c9500", parse("00050000101c9500", 208, "update").id)
        assertThrows(IllegalArgumentException::class.java) { parse("00050000101c9500", 208, "game") }
        assertThrows(IllegalArgumentException::class.java) { parse("0005000e101c9500", 176, "update") }
        assertThrows(IllegalArgumentException::class.java) { parse("0005000c101c9400", 80, "dlc", "00050000101c9500") }
        assertThrows(IllegalArgumentException::class.java) { parse("0005000012345678", 0, "game") }
    }
    @Test fun gameUpdateAndDlcInstallAndFailedReplacementKeepsOldFiles() = runBlocking {
        val app = ApplicationProvider.getApplicationContext<Application>()
        val root = File(app.cacheDir, "import-test-${UUID.randomUUID()}").apply { mkdirs() }
        try {
            val source = root.resolve("source").apply { mkdirs() }
            for (folder in listOf("code", "content", "meta")) source.resolve(folder).mkdirs()
            source.resolve("code/U-King.rpx").writeText("synthetic test data")
            source.resolve("content/test.bin").writeText("content")
            val meta = source.resolve("meta/meta.xml")
            val mlc = root.resolve("mlc").apply { mkdirs() }
            meta.writeText(xml("00050000101c9500", 0))
            val (_, target) = HyruleInstaller.installTitle(HyruleLauncherState.localSource(source), "game", mlc, null) { _, _, _ -> }
            assertEquals("synthetic test data", target.resolve("code/U-King.rpx").readText())
            meta.writeText(xml("00050000101c9500", 208))
            val (_, update) = HyruleInstaller.installTitle(HyruleLauncherState.localSource(source), "update", mlc, "00050000101c9500") { _, _, _ -> }
            assertTrue(update.path.contains("0005000e"))
            meta.writeText(xml("0005000c101c9500", 80))
            val (_, dlc) = HyruleInstaller.installTitle(HyruleLauncherState.localSource(source), "dlc", mlc, "00050000101c9500") { _, _, _ -> }
            assertTrue(dlc.path.contains("0005000c"))
            val broken = object : HyruleSource {
                override val name = "failure.bin"; override val directory = false; override val size = 1L
                override fun children() = emptyList<HyruleSource>()
                override fun open(): java.io.InputStream = throw IOException("simulated provider failure")
            }
            try { HyruleInstaller.copyAtomically(listOf(broken), target) { _, _, _ -> }; fail("Expected a read failure") } catch (_: IOException) { }
            assertEquals("synthetic test data", target.resolve("code/U-King.rpx").readText())
            assertFalse(target.parentFile!!.listFiles()!!.any { it.name.contains(".install-") })
        } finally { root.deleteRecursively() }
    }
}
