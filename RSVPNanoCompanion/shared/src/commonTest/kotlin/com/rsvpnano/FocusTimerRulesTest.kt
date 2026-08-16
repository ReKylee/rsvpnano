package com.rsvpnano

import com.rsvpnano.models.NanoFocusTimer
import com.rsvpnano.models.NanoFocusTimerRules
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class FocusTimerRulesTest {
    @Test
    fun validatesTheSameUtf8NameAndValueLimitsAsTheReader() {
        assertTrue(NanoFocusTimerRules.valid(NanoFocusTimer("אבגדהוז", 25, 5, 4)))
        assertFalse(NanoFocusTimerRules.valid(NanoFocusTimer("אבגדהוזח", 25, 5, 4)))
        assertFalse(NanoFocusTimerRules.valid(NanoFocusTimer("Bad\nname", 25, 5, 4)))
        assertFalse(NanoFocusTimerRules.valid(NanoFocusTimer("Timer", 0, 5, 4)))
        assertFalse(NanoFocusTimerRules.valid(NanoFocusTimer("Timer", 25, 61, 4)))
        assertFalse(NanoFocusTimerRules.valid(NanoFocusTimer("Timer", 25, 5, 13)))
    }
}
