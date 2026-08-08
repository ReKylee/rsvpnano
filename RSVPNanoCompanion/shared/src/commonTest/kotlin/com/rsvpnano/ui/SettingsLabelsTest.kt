package com.rsvpnano.ui

import kotlin.test.Test
import kotlin.test.assertEquals

class SettingsLabelsTest {
    @Test
    fun fontCapabilitiesUseReaderFriendlyScriptNames() {
        assertEquals("Hebrew · Shaping", fontDetails(1 shl 3, builtIn = false, shaping = true))
        assertEquals("Arabic · Shaping", fontDetails(1 shl 4, builtIn = false, shaping = true))
        assertEquals("Latin · Cyrillic · Built in", fontDetails(3, builtIn = true, shaping = false))
    }
}
