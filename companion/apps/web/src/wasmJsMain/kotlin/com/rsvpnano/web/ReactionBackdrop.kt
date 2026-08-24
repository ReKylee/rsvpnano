@file:OptIn(ExperimentalWasmJsInterop::class)

package com.rsvpnano.web

import androidx.compose.foundation.Canvas
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.layout.onSizeChanged
import kotlinx.coroutines.delay
import kotlin.math.ceil
import kotlin.math.min

private const val ReactionShortAxisCells = 30
private const val ReactionStateCount = 12

@Composable
internal fun ReactionBackdrop(modifier: Modifier = Modifier) {
    var grid by remember { mutableStateOf(ReactionShortAxisCells to ReactionShortAxisCells) }
    val (columns, rows) = grid
    val cells = remember(columns, rows) { seededReactionCells(columns * rows) }
    val next = remember(columns, rows) { IntArray(cells.size) }
    val statePaths = remember { Array(4) { Path() } }
    var generation by remember { mutableIntStateOf(0) }
    val reducedMotion = remember { prefersReducedMotion() }

    LaunchedEffect(reducedMotion, columns, rows) {
        if (!reducedMotion) {
            while (true) {
                delay(140)
                stepReaction(cells, next, columns, rows)
                generation++
            }
        }
    }

    val bright = MaterialTheme.colorScheme.tertiary
    val dim = MaterialTheme.colorScheme.primary
    Canvas(
        modifier.onSizeChanged { size ->
            val resized = reactionGridDimensions(size.width, size.height)
            if (resized != grid) grid = resized
        }.clipToBounds(),
    ) {
        @Suppress("UNUSED_EXPRESSION")
        generation
        statePaths.forEach(Path::reset)
        val cellSize = min(size.width / columns, size.height / rows)
        val originX = (size.width - cellSize * columns) / 2f
        val originY = (size.height - cellSize * rows) / 2f
        val inset = cellSize * 0.14f
        cells.forEachIndexed { index, state ->
            if (state !in 1..4) return@forEachIndexed
            val x = index % columns
            val y = index / columns
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

internal fun reactionGridDimensions(width: Int, height: Int): Pair<Int, Int> {
    if (width <= 0 || height <= 0) return ReactionShortAxisCells to ReactionShortAxisCells
    return if (width <= height) {
        ReactionShortAxisCells to ceil(ReactionShortAxisCells * height.toDouble() / width).toInt()
    } else {
        ceil(ReactionShortAxisCells * width.toDouble() / height).toInt() to ReactionShortAxisCells
    }
}

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
