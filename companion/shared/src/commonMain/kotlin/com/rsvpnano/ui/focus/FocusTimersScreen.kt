package com.rsvpnano.ui.focus

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.Edit
import androidx.compose.material.icons.outlined.Timer
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.rsvpnano.models.NanoFocusTimer
import com.rsvpnano.models.NanoFocusTimerRules
import com.rsvpnano.models.NanoFocusTimers
import com.rsvpnano.presentation.CompanionResource
import com.rsvpnano.presentation.CompanionUiState
import com.rsvpnano.ui.INLINE_DIVIDER
import com.rsvpnano.ui.SettingsSection
import com.rsvpnano.ui.settings.SettingsPage

@Composable
internal fun FocusTimersSettings(
    uiState: CompanionUiState,
    onSave: (NanoFocusTimers) -> Unit,
) {
    var editingIndex by remember { mutableStateOf<Int?>(null) }
    var creating by remember { mutableStateOf(false) }
    val timers = uiState.focusTimers.timers

    SettingsPage {
        SettingsSection(
            title = "Focus routines",
            subtitle = "Choose how long to focus and rest, and how many rounds to repeat.",
        ) {
            if (!uiState.isConnected) {
                Text(
                    "Connect to your Nano to edit focus timers.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else if (CompanionResource.FocusTimers !in uiState.loadedResources) {
                if (CompanionResource.FocusTimers in uiState.loadingResources) {
                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                } else {
                    Text(
                        "Focus routines could not be loaded.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            } else {
                timers.forEachIndexed { index, timer ->
                    ListItem(
                        headlineContent = { Text(timer.name) },
                        supportingContent = {
                            Text(
                                listOf(
                                    "${timer.focusMinutes} min focus",
                                    "${timer.breakMinutes} min break",
                                    "${timer.rounds} rounds",
                                ).joinToString(INLINE_DIVIDER),
                            )
                        },
                        leadingContent = { Icon(Icons.Outlined.Timer, contentDescription = null) },
                        trailingContent = { Icon(Icons.Outlined.Edit, contentDescription = null) },
                        modifier = Modifier.clickable { editingIndex = index },
                    )
                    if (index != timers.lastIndex) HorizontalDivider(modifier = Modifier.padding(start = 56.dp))
                }
                if (timers.size < NanoFocusTimerRules.MAX_TIMERS) {
                    FilledTonalButton(
                        onClick = { creating = true },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Icon(Icons.Outlined.Add, contentDescription = null)
                        Text("Add routine", modifier = Modifier.padding(start = 8.dp))
                    }
                }
            }
        }
    }

    val index = editingIndex
    val draft = when {
        creating -> NanoFocusTimer(name = "Pomodoro")
        index != null -> timers.getOrNull(index)
        else -> null
    }
    if (draft != null) {
        FocusTimerEditor(
            initial = draft,
            canDelete = !creating && timers.size > 1,
            onDismiss = {
                editingIndex = null
                creating = false
            },
            onSave = { timer ->
                val updated = if (creating) {
                    timers + timer
                } else {
                    timers.mapIndexed { candidate, current -> if (candidate == index) timer else current }
                }
                onSave(NanoFocusTimers(updated))
                editingIndex = null
                creating = false
            },
            onDelete = if (!creating && index != null && timers.size > 1) {
                {
                    onSave(NanoFocusTimers(timers.filterIndexed { candidate, _ -> candidate != index }))
                    editingIndex = null
                }
            } else null,
        )
    }
}

@Composable
private fun FocusTimerEditor(
    initial: NanoFocusTimer,
    canDelete: Boolean,
    onDismiss: () -> Unit,
    onSave: (NanoFocusTimer) -> Unit,
    onDelete: (() -> Unit)?,
) {
    var timer by remember(initial) { mutableStateOf(initial) }
    var confirmingDelete by remember { mutableStateOf(false) }
    if (confirmingDelete && onDelete != null) {
        AlertDialog(
            onDismissRequest = { confirmingDelete = false },
            title = { Text("Delete focus routine?") },
            text = { Text("Remove ${timer.name} from the reader? This cannot be undone.") },
            confirmButton = {
                TextButton(
                    onClick = onDelete,
                    colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                ) {
                    Text("Delete")
                }
            },
            dismissButton = { TextButton(onClick = { confirmingDelete = false }) { Text("Cancel") } },
        )
        return
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Outlined.Timer, contentDescription = null) },
        title = { Text(if (onDelete == null) "New focus routine" else "Edit focus routine") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = timer.name,
                    onValueChange = { value ->
                        if (value.encodeToByteArray().size <= NanoFocusTimerRules.MAX_NAME_BYTES) {
                            timer = timer.copy(name = value)
                        }
                    },
                    label = { Text("Name") },
                    supportingText = { Text("${timer.name.encodeToByteArray().size}/${NanoFocusTimerRules.MAX_NAME_BYTES} bytes") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                TimerStepper(
                    label = "Focus",
                    value = timer.focusMinutes,
                    range = NanoFocusTimerRules.MIN_FOCUS_MINUTES..NanoFocusTimerRules.MAX_FOCUS_MINUTES,
                    suffix = "min",
                    onValue = { timer = timer.copy(focusMinutes = it) },
                )
                TimerStepper(
                    label = "Break",
                    value = timer.breakMinutes,
                    range = NanoFocusTimerRules.MIN_BREAK_MINUTES..NanoFocusTimerRules.MAX_BREAK_MINUTES,
                    suffix = "min",
                    onValue = { timer = timer.copy(breakMinutes = it) },
                )
                TimerStepper(
                    label = "Rounds",
                    value = timer.rounds,
                    range = NanoFocusTimerRules.MIN_ROUNDS..NanoFocusTimerRules.MAX_ROUNDS,
                    suffix = "",
                    onValue = { timer = timer.copy(rounds = it) },
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = { onSave(timer.copy(name = timer.name.trim())) },
                enabled = NanoFocusTimerRules.valid(timer.copy(name = timer.name.trim())),
            ) {
                Text(if (onDelete == null) "Add" else "Save")
            }
        },
        dismissButton = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (canDelete && onDelete != null) {
                    TextButton(
                        onClick = { confirmingDelete = true },
                        colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                    ) {
                        Text("Delete")
                    }
                }
                TextButton(onClick = onDismiss) { Text("Cancel") }
            }
        },
    )
}

@Composable
private fun TimerStepper(
    label: String,
    value: Int,
    range: IntRange,
    suffix: String,
    onValue: (Int) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f), style = MaterialTheme.typography.labelLarge)
        TextButton(onClick = { onValue(value - 1) }, enabled = value > range.first) { Text("−") }
        Text(
            "$value${suffix.takeIf(String::isNotEmpty)?.let { " $it" }.orEmpty()}",
            style = MaterialTheme.typography.titleMedium,
        )
        TextButton(onClick = { onValue(value + 1) }, enabled = value < range.last) { Text("+") }
    }
}
