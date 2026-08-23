@file:OptIn(ExperimentalWasmJsInterop::class)

package com.rsvpnano.web

import androidx.compose.foundation.Canvas
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Path
import kotlinx.coroutines.delay
import kotlin.math.max

private const val ReactionColumns = 42
private const val ReactionRows = 30
private const val ReactionStateCount = 12

@Composable
internal fun ReactionBackdrop(modifier: Modifier = Modifier) {
    val cells = remember { seededReactionCells(ReactionColumns * ReactionRows) }
    val next = remember { IntArray(cells.size) }
    val statePaths = remember { Array(4) { Path() } }
    var generation by remember { mutableIntStateOf(0) }
    val reducedMotion = remember { prefersReducedMotion() }

    LaunchedEffect(reducedMotion) {
        if (!reducedMotion) {
            while (true) {
                delay(140)
                stepReaction(cells, next, ReactionColumns, ReactionRows)
                generation++
            }
        }
    }

    val bright = MaterialTheme.colorScheme.tertiary
    val dim = MaterialTheme.colorScheme.primary
    Canvas(modifier) {
        @Suppress("UNUSED_EXPRESSION")
        generation
        statePaths.forEach(Path::reset)
        val cellSize = reactionCellSize(size.width, size.height, ReactionColumns, ReactionRows)
        val originX = (size.width - cellSize * ReactionColumns) / 2f
        val originY = (size.height - cellSize * ReactionRows) / 2f
        val inset = cellSize * 0.14f
        cells.forEachIndexed { index, state ->
            if (state !in 1..4) return@forEachIndexed
            val x = index % ReactionColumns
            val y = index / ReactionColumns
            val left = originX + x * cellSize + inset
            val top = originY + y * cellSize + inset
            statePaths[state - 1].addRect(
                Rect(left, top, left + cellSize - inset * 2, top + cellSize - inset * 2),
            )
        }
        statePaths.forEachIndexed { index, path ->
            drawPath(
                path,
                if (index == 0) bright.copy(alpha = 0.88f) else dim.copy(alpha = 0.37f - index * 0.07f),
            )
        }
    }
}

internal fun reactionCellSize(width: Float, height: Float, columns: Int, rows: Int): Float =
    max(width / columns, height / rows)

internal fun seededReactionCells(size: Int): IntArray {
    var seed = 0x52_53_56_50
    return IntArray(size) {
        seed = seed xor (seed shl 13)
        seed = seed xor (seed ushr 17)
        seed = seed xor (seed shl 5)
        (seed ushr 24) % ReactionStateCount
    }
}

internal fun stepReaction(cells: IntArray, next: IntArray, columns: Int, rows: Int) {
    for (y in 0 until rows) {
        for (x in 0 until columns) {
            val index = y * columns + x
            val nextState = (cells[index] + 1) % ReactionStateCount
            var advances = false
            neighbors@ for (dy in -1..1) {
                for (dx in -1..1) {
                    if (dx == 0 && dy == 0) continue
                    val neighborX = (x + dx + columns) % columns
                    val neighborY = (y + dy + rows) % rows
                    if (cells[neighborY * columns + neighborX] == nextState) {
                        advances = true
                        break@neighbors
                    }
                }
            }
            next[index] = if (advances) nextState else cells[index]
        }
    }
    next.copyInto(cells)
}

@JsFun("() => window.matchMedia('(prefers-reduced-motion: reduce)').matches")
internal external fun prefersReducedMotion(): Boolean
