package com.rsvpnano.ui

import kotlin.test.Test
import kotlin.test.assertEquals

class CompanionNavigationTest {
    @Test
    fun backFollowsTheVisibleScreenHierarchy() {
        assertEquals(
            CompanionScreen.Settings to null,
            previousScreen(CompanionScreen.Settings, SettingsDestination.Fonts, wide = false),
        )
        assertEquals(
            CompanionScreen.Library to null,
            previousScreen(CompanionScreen.Settings, null, wide = false),
        )
        assertEquals(
            CompanionScreen.Library to SettingsDestination.Fonts,
            previousScreen(CompanionScreen.Settings, SettingsDestination.Fonts, wide = true),
        )
    }
}
