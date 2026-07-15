package com.rsvpnano.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.MenuBook
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.CloudUpload
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.Edit
import androidx.compose.material.icons.outlined.Newspaper
import androidx.compose.material.icons.outlined.RssFeed
import androidx.compose.material.icons.outlined.Sync
import androidx.compose.material.icons.outlined.UploadFile
import androidx.compose.material.icons.outlined.WarningAmber
import androidx.compose.material.icons.outlined.Wifi
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import com.rsvpnano.app.CompanionNotice
import com.rsvpnano.app.CompanionNotice.Attention
import com.rsvpnano.app.CompanionNotice.Error
import com.rsvpnano.app.CompanionNotice.Success
import com.rsvpnano.converters.RsvpSupportedFileTypes
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.PendingUpload
import kotlin.math.roundToInt

private enum class LibraryFilter(val label: String) {
    All("All"),
    Books("Books"),
    Articles("Articles"),
}


@Composable
fun LibraryTab(
    uiState: CompanionUiState,
    onRefresh: () -> Unit,
    needsArticleFetch: (PendingUpload) -> Boolean,
    onEditDraft: (PendingUpload) -> Unit,
    onDeleteDraft: (PendingUpload) -> Unit,
    onSyncArticles: () -> Unit,
    onDeleteBook: (NanoBook) -> Unit,
    onSetBookPosition: (NanoBook, Int) -> Unit,
    onShowUpload: () -> Unit,
) {
    var searchQuery by remember { mutableStateOf("") }
    var filter by remember { mutableStateOf(LibraryFilter.All) }
    var selectedBook by remember { mutableStateOf<NanoBook?>(null) }
    var bookToDelete by remember { mutableStateOf<NanoBook?>(null) }
    var draftToDelete by remember { mutableStateOf<PendingUpload?>(null) }
    val visibleDrafts = uiState.drafts.filter { draft ->
        val query = searchQuery.trim()
        filter != LibraryFilter.Books &&
            (
                query.isEmpty() ||
                    draft.title.contains(query, ignoreCase = true) ||
                    draft.sourceUrl.orEmpty().contains(query, ignoreCase = true)
                )
    }
    val visibleBooks = uiState.books.filter { book ->
        val isArticle = book.isArticle
        val matchesFilter = when (filter) {
            LibraryFilter.All -> true
            LibraryFilter.Books -> !isArticle
            LibraryFilter.Articles -> isArticle
        }
        val query = searchQuery.trim()
        val matchesQuery = query.isEmpty() ||
            book.displayTitle.contains(query, ignoreCase = true) ||
            book.metadata.author.contains(query, ignoreCase = true) ||
            book.name.contains(query, ignoreCase = true)
        matchesFilter && matchesQuery
    }
    PullRefreshBox(
        isRefreshing = uiState.isRefreshing,
        onRefresh = onRefresh,
    ) {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(bottom = 18.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            item {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    if (uiState.isConnected) {
                        UploadLibraryRow(onClick = onShowUpload)
                    }
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        label = { Text("Search library") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        LibraryFilter.entries.forEach { option ->
                            FilterChip(
                                selected = filter == option,
                                onClick = { filter = option },
                                label = { Text(option.label) },
                            )
                        }
                    }
                    HorizontalDivider()
                }
            }

            if (visibleDrafts.isNotEmpty()) {
                item {
                    Text(
                        text = "Pending articles",
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                items(visibleDrafts, key = { draft -> draft.id }) { draft ->
                    PendingArticleRow(
                        draft = draft,
                        needsFetch = needsArticleFetch(draft),
                        onEdit = { onEditDraft(draft) },
                        onDelete = { draftToDelete = draft },
                    )
                }
                item {
                    Button(
                        onClick = onSyncArticles,
                        enabled = uiState.isConnected && uiState.drafts.any { !needsArticleFetch(it) },
                    ) {
                        Icon(imageVector = Icons.Outlined.Sync, contentDescription = null)
                        Text("Sync ready articles")
                    }
                }
            }

            if (visibleBooks.isEmpty()) {
                item {
                    EmptyCard(
                        text = when {
                            !uiState.isConnected -> "Reader library unavailable while disconnected."
                            visibleDrafts.isNotEmpty() -> "No matching reader items."
                            else -> "No library items on the reader."
                        },
                    )
                }
            } else {
                item {
                    Text(
                        text = "Library",
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                items(visibleBooks, key = { book -> book.id }) { book ->
                    LibraryBookRow(
                        book = book,
                        onOpenBook = { selectedBook = book },
                        onDeleteBook = { bookToDelete = book },
                    )
                }
            }
        }
    }

    selectedBook?.let { book ->
        LibraryBookDialog(
            book = book,
            onDismiss = { selectedBook = null },
            onSetPosition = { wordIndex ->
                selectedBook = null
                onSetBookPosition(book, wordIndex)
            },
        )
    }

    bookToDelete?.let { book ->
        AlertDialog(
            onDismissRequest = { bookToDelete = null },
            icon = {
                Icon(imageVector = Icons.Outlined.Delete, contentDescription = null)
            },
            title = { Text("Delete from reader?") },
            text = { Text(book.displayTitle) },
            confirmButton = {
                FilledTonalButton(
                    onClick = {
                        bookToDelete = null
                        onDeleteBook(book)
                    },
                    colors = ButtonDefaults.filledTonalButtonColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer,
                        contentColor = MaterialTheme.colorScheme.onErrorContainer,
                    ),
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { bookToDelete = null }) {
                    Text("Cancel")
                }
            },
        )
    }

    draftToDelete?.let { draft ->
        AlertDialog(
            onDismissRequest = { draftToDelete = null },
            icon = {
                Icon(imageVector = Icons.Outlined.Delete, contentDescription = null)
            },
            title = { Text("Delete saved article?") },
            text = { Text(draft.title) },
            confirmButton = {
                FilledTonalButton(
                    onClick = {
                        draftToDelete = null
                        onDeleteDraft(draft)
                    },
                    colors = ButtonDefaults.filledTonalButtonColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer,
                        contentColor = MaterialTheme.colorScheme.onErrorContainer,
                    ),
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { draftToDelete = null }) {
                    Text("Cancel")
                }
            },
        )
    }
}

@Composable
private fun UploadLibraryRow(
    onClick: () -> Unit,
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        color = MaterialTheme.colorScheme.primaryContainer,
        shape = MaterialTheme.shapes.small,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                imageVector = Icons.Outlined.UploadFile,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
            )
            Text(
                text = "Upload",
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onPrimaryContainer,
            )
        }
    }
}

@Composable
private fun PendingArticleRow(
    draft: PendingUpload,
    needsFetch: Boolean,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow),
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Icon(
                    imageVector = Icons.Outlined.Newspaper,
                    contentDescription = null,
                    tint = if (needsFetch) MaterialTheme.colorScheme.tertiary else MaterialTheme.colorScheme.primary,
                )
                Column(modifier = Modifier.weight(1f)) {
                    Text(text = draft.title, style = MaterialTheme.typography.titleSmall)
                    Text(
                        text = pendingArticleMeta(draft = draft, needsFetch = needsFetch),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = onEdit) {
                    Icon(imageVector = Icons.Outlined.Edit, contentDescription = null)
                    Text("Edit")
                }
                FilledTonalButton(
                    onClick = onDelete,
                    colors = ButtonDefaults.filledTonalButtonColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer,
                        contentColor = MaterialTheme.colorScheme.onErrorContainer,
                    ),
                ) {
                    Text("Delete")
                }
            }
        }
    }
}

@Composable
private fun LibraryBookRow(
    book: NanoBook,
    onOpenBook: () -> Unit,
    onDeleteBook: (NanoBook) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onOpenBook)
            .padding(vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Icon(
                imageVector = if (book.isArticle) Icons.Outlined.Newspaper else Icons.AutoMirrored.Outlined.MenuBook,
                contentDescription = null,
                tint = if (book.isArticle) MaterialTheme.colorScheme.tertiary else MaterialTheme.colorScheme.secondary,
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(text = book.displayTitle, style = MaterialTheme.typography.titleSmall)
                Text(
                    text = book.libraryMetaLabel,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            DestructiveIconButton(
                contentDescription = "Delete",
                onClick = { onDeleteBook(book) },
            )
        }
        book.reading?.percent?.let { progress ->
            LinearProgressIndicator(
                progress = { (progress.coerceIn(0, 100) / 100f) },
                modifier = Modifier.fillMaxWidth(),
            )
            Text(text = "$progress% read", style = MaterialTheme.typography.labelSmall)
        }
        HorizontalDivider()
    }
}

@Composable
private fun LibraryBookDialog(
    book: NanoBook,
    onDismiss: () -> Unit,
    onSetPosition: (Int) -> Unit,
) {
    var showPositionDialog by remember(book.id) { mutableStateOf(false) }
    val metadata = book.metadata
    val reading = book.reading

    if (!showPositionDialog) {
        AlertDialog(
            onDismissRequest = onDismiss,
            icon = {
                Icon(
                    imageVector = if (book.isArticle) Icons.Outlined.Newspaper else Icons.AutoMirrored.Outlined.MenuBook,
                    contentDescription = null,
                )
            },
            title = { Text(text = book.displayTitle) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    metadata.author.takeIf { it.isNotBlank() }?.let { MetadataLine("Author", it) }
                    reading?.currentChapter?.let { MetadataLine("Current chapter", "${it.number}. ${it.title}") }
                    MetadataLine("Length", "${metadata.wordCount} words · ${metadata.chapterCount} chapters")
                    reading?.let {
                        MetadataLine("Progress", "${it.percent}% · ${it.remainingWords} words remaining")
                        MetadataLine("Reading time", "About ${it.estimatedMinutes} min remaining")
                    }
                    MetadataLine("File", "${book.byteLabel} · ${book.name}")
                    if (book.source != null && metadata.wordCount > 0) {
                        FilledTonalButton(onClick = { showPositionDialog = true }) {
                            Text("Change reading position")
                        }
                    } else {
                        Text(
                            text = "Reading position becomes available after the reader indexes this book.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = onDismiss) {
                    Text(text = "Close")
                }
            },
        )
    }

    if (showPositionDialog) {
        ReadingPositionDialog(
            book = book,
            onDismiss = { showPositionDialog = false },
            onSave = onSetPosition,
        )
    }
}

@Composable
private fun MetadataLine(label: String, value: String) {
    Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
        Text(text = label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(text = value, style = MaterialTheme.typography.bodyMedium)
    }
}

@Composable
private fun ReadingPositionDialog(
    book: NanoBook,
    onDismiss: () -> Unit,
    onSave: (Int) -> Unit,
) {
    val wordCount = book.metadata.wordCount
    val chapters = book.metadata.chapters.filter { it.wordIndex in 0 until wordCount }.sortedBy { it.wordIndex }
    val initialIndex = (book.reading?.wordIndex ?: 0).coerceIn(0, wordCount - 1)
    fun percentForIndex(index: Int) = if (wordCount <= 1) 0 else
        ((index.toFloat() / (wordCount - 1)) * 100).roundToInt().coerceIn(0, 100)
    fun indexForPercent(percent: Int) = if (wordCount <= 1) 0 else
        ((percent.coerceIn(0, 100) / 100f) * (wordCount - 1)).roundToInt().coerceIn(0, wordCount - 1)
    var targetIndex by remember(book.id) { mutableStateOf(initialIndex) }
    var targetPercent by remember(book.id) { mutableStateOf(percentForIndex(initialIndex)) }
    val chapterIndex = chapters.indexOfLast { it.wordIndex <= targetIndex }.coerceAtLeast(0)
    fun moveTo(index: Int) {
        targetIndex = index.coerceIn(0, wordCount - 1)
        targetPercent = percentForIndex(targetIndex)
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Reading position") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("$targetPercent%", style = MaterialTheme.typography.headlineSmall)
                Text("Word ${targetIndex + 1} of $wordCount", color = MaterialTheme.colorScheme.onSurfaceVariant)
                Slider(
                    value = targetPercent.toFloat(),
                    onValueChange = {
                        targetPercent = it.roundToInt().coerceIn(0, 100)
                        targetIndex = indexForPercent(targetPercent)
                    },
                    valueRange = 0f..100f,
                    steps = 99,
                )
                if (chapters.isNotEmpty()) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        IconButton(
                            onClick = { moveTo(chapters[chapterIndex - 1].wordIndex) },
                            enabled = chapterIndex > 0,
                        ) {
                            Icon(Icons.AutoMirrored.Outlined.KeyboardArrowLeft, contentDescription = "Previous chapter")
                        }
                        Column(modifier = Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                            Text("Chapter ${chapterIndex + 1} of ${chapters.size}", style = MaterialTheme.typography.labelSmall)
                            Text(chapters[chapterIndex].title, style = MaterialTheme.typography.bodySmall)
                        }
                        IconButton(
                            onClick = { moveTo(chapters[chapterIndex + 1].wordIndex) },
                            enabled = chapterIndex < chapters.lastIndex,
                        ) {
                            Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, contentDescription = "Next chapter")
                        }
                    }
                }
            }
        },
        confirmButton = {
            Button(onClick = { onSave(targetIndex) }) { Text("Save") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

val NanoBook.isArticle: Boolean
    get() = category == "article"

val NanoBook.libraryMetaLabel: String
    get() = listOfNotNull(
        metadata.author.takeIf { it.isNotBlank() },
        metadata.wordCount.takeIf { it > 0 }?.let { "$it words" },
        reading?.percent?.let { "$it% read" },
        byteLabel,
        name.takeIf { displayTitle != name.substringAfterLast('/') },
    ).joinToString(" · ").ifBlank { id }

val NanoBook.byteLabel: String
    get() = bytes.toByteLabel()

fun pendingArticleMeta(draft: PendingUpload, needsFetch: Boolean): String {
    val state = if (needsFetch) "Needs article text" else "Ready to sync"
    val source = draft.sourceUrl?.takeIf { it.isNotBlank() }?.substringAfter("://")?.substringBefore("/")
    return listOfNotNull(state, draft.body.encodeToByteArray().size.toByteLabel(), source).joinToString(" · ")
}

fun Int.toByteLabel(): String {
    return when {
        this < 1024 -> "$this B"
        this < 1024 * 1024 -> "${oneDecimal(this / 1024.0)} KB"
        else -> "${oneDecimal(this / (1024.0 * 1024.0))} MB"
    }
}

private fun oneDecimal(value: Double): String {
    val scaled = (value * 10.0).toInt()
    return "${scaled / 10}.${scaled % 10}"
}

