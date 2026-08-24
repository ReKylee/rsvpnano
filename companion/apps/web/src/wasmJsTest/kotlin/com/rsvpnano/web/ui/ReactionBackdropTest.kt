package com.rsvpnano.web.ui

import com.rsvpnano.web.ui.reactionGridDimensions
import com.rsvpnano.web.ui.seededReactionCells
import com.rsvpnano.web.ui.stepReaction
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ReactionBackdropTest {
    @Test
    fun reactionStepIsDeterministicAndKeepsStatesInRange() {
        val first = seededReactionCells(12 * 8)
        val second = seededReactionCells(12 * 8)
        val before = first.copyOf()

        stepReaction(first, IntArray(first.size), 12, 8)
        stepReaction(second, IntArray(second.size), 12, 8)

        assertContentEquals(first, second)
        assertFalse(first.contentEquals(before))
        assertTrue(first.all { it in 0 until 12 })
    }

    @Test
    fun reactionGridTracksThePanelShape() {
        assertEquals(30 to 60, reactionGridDimensions(width = 300, height = 600))
        assertEquals(60 to 30, reactionGridDimensions(width = 600, height = 300))
        assertEquals(30 to 30, reactionGridDimensions(width = 0, height = 0))
    }
}
