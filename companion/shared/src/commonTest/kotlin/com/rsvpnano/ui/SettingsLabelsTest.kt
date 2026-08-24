package com.rsvpnano.ui

import kotlin.test.Test
import kotlin.test.assertEquals

class SettingsLabelsTest {
    @Test
    fun fontCapabilitiesUseReaderFriendlyScriptNames() {
        assertEquals("Hebrew  │  Shaping", fontDetails(listOf("Hebr"), builtIn = false, shaping = true))
        assertEquals("Arabic  │  Shaping", fontDetails(listOf("Arab"), builtIn = false, shaping = true))
        assertEquals(
            "Latin  │  Cyrillic  │  Built in",
            fontDetails(listOf("Latn", "Cyrl"), builtIn = true, shaping = false),
        )
    }
}
