package info.cemu.cemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withText
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class HyruleSessionTest {
    @Test fun packagedClientLoadsWithoutStartingSession() {
        System.loadLibrary("MilkBarClient")
        // Any unresolved native dependency fails this test at loadLibrary.
    }

    @Test fun connectionDialogAppearsOnLaunch() {
        ActivityScenario.launch(MainActivity::class.java).use {
            onView(withText("Hyrule Together — connection")).check(matches(isDisplayed()))
        }
    }

    @Test fun jniRejectsInvalidConnectionWithoutStartingWorkers() {
        assertFalse(HyruleSession.start("", "", 0, "", ""))
        assertEquals("Invalid server or player settings", HyruleSession.status())
        assertFalse(HyruleSession.start("/unused", "host;injected", 5050, "Android", ""))
        assertFalse(HyruleSession.start("/unused", "127.0.0.1", 65536, "Android", ""))
        assertFalse(HyruleSession.start("/unused", "127.0.0.1", 5050, "[Android", ""))
    }

    @Test fun unrecognizedGameDoesNotPassBotwGate() {
        assertFalse(HyruleSession.isBotw("/does-not-exist/U-King.rpx"))
    }
}
