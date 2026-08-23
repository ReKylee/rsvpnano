package com.rsvpnano.web

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
    fun reactionCellsCoverThePanelWithoutStretching() {
        assertEquals(20f, reactionCellSize(width = 520f, height = 240f, columns = 26, rows = 18))
        assertEquals(20f, reactionCellSize(width = 260f, height = 360f, columns = 26, rows = 18))
    }
}
