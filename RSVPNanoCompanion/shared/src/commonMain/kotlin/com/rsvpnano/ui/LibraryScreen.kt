package com.rsvpnano.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.MenuBook
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.Language
import androidx.compose.material.icons.outlined.MoreVert
import androidx.compose.material.icons.outlined.Newspaper
import androidx.compose.material.icons.outlined.Sync
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoBookLanguage
import com.rsvpnano.models.NanoFontSummary
import com.rsvpnano.models.NanoLanguageFont
import com.rsvpnano.models.PendingUpload
import kotlin.math.roundToInt

private enum class LibraryFilter(val label: String) {
    All("All"),
    Books("Books"),
    Articles("Articles"),
}


@Composable
internal fun LibraryScreen(
    uiState: CompanionUiState,
    onRefresh: () -> Unit,
    needsArticleFetch: (PendingUpload) -> Boolean,
    onEditDraft: (PendingUpload) -> Unit,
    onDeleteDraft: (PendingUpload) -> Unit,
    onSyncArticles: () -> Unit,
    onDeleteBook: (NanoBook) -> Unit,
    onSetBookPosition: (NanoBook, Int) -> Unit,
    onSetBookLanguageFonts: (NanoBook, List<NanoLanguageFont>) -> Unit,
    onAddContent: () -> Unit,
) {
    var searchQuery by rememberSaveable { mutableStateOf("") }
    var filterName by rememberSaveable { mutableStateOf(LibraryFilter.All.name) }
    val filter = LibraryFilter.valueOf(filterName)
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
                                onClick = { filterName = option.name },
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
                        text = "Queued articles",
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
                    LibraryEmptyState(
                        text = when {
                            !uiState.isConnected -> "Connect to view the reader library."
                            searchQuery.isNotBlank() || filter != LibraryFilter.All -> "No items match this search."
                            visibleDrafts.isNotEmpty() -> "No other items are on the reader yet."
                            else -> "Your reader library is empty."
                        },
                        onAddContent = onAddContent.takeIf { searchQuery.isBlank() && filter == LibraryFilter.All },
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
            availableFonts = uiState.availableFonts,
            globalFontId = uiState.settings?.reading?.typography?.fontId.orEmpty(),
            onSetLanguageFonts = { selections ->
                selectedBook = null
                onSetBookLanguageFonts(book, selections)
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
private fun PendingArticleRow(
    draft: PendingUpload,
    needsFetch: Boolean,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onEdit)
            .padding(vertical = 10.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = Icons.Outlined.Newspaper,
            contentDescription = null,
            tint = if (needsFetch) MaterialTheme.colorScheme.tertiary else MaterialTheme.colorScheme.primary,
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(text = draft.title, style = MaterialTheme.typography.titleSmall)
            Text(
                text = listOfNotNull(
                    if (needsFetch) "Queued for download" else "Ready to sync",
                    draft.sourceUrl?.takeIf(String::isNotBlank)?.substringAfter("://")?.substringBefore('/'),
                ).joinToString(" · "),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Box {
            IconButton(onClick = { menuExpanded = true }) {
                Icon(Icons.Outlined.MoreVert, contentDescription = "Article actions")
            }
            DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                DropdownMenuItem(
                    text = { Text("Edit") },
                    onClick = {
                        menuExpanded = false
                        onEdit()
                    },
                )
                DropdownMenuItem(
                    text = { Text("Delete") },
                    onClick = {
                        menuExpanded = false
                        onDelete()
                    },
                )
            }
        }
    }
    HorizontalDivider()
}

@Composable
private fun LibraryEmptyState(text: String, onAddContent: (() -> Unit)?) {
    Column(
        modifier = Modifier.fillMaxWidth().padding(vertical = 48.dp, horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Icon(
            Icons.AutoMirrored.Outlined.MenuBook,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(text, color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (onAddContent != null) {
            TextButton(onClick = onAddContent) { Text("Add content") }
        }
    }
}

@Composable
private fun LibraryBookRow(
    book: NanoBook,
    onOpenBook: () -> Unit,
    onDeleteBook: (NanoBook) -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onOpenBook)
            .padding(vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                imageVector = if (book.isArticle) Icons.Outlined.Newspaper else Icons.AutoMirrored.Outlined.MenuBook,
                contentDescription = null,
                tint = if (book.isArticle) MaterialTheme.colorScheme.tertiary else MaterialTheme.colorScheme.secondary,
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(text = book.displayTitle, style = MaterialTheme.typography.titleSmall)
                Text(
                    text = book.librarySubtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                )
            }
            Box {
                IconButton(onClick = { menuExpanded = true }) {
                    Icon(Icons.Outlined.MoreVert, contentDescription = "Book actions")
                }
                DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                    DropdownMenuItem(
                        text = { Text("Delete from reader") },
                        onClick = {
                            menuExpanded = false
                            onDeleteBook(book)
                        },
                    )
                }
            }
        }
        book.reading?.percent?.let { progress ->
            LinearProgressIndicator(
                progress = { (progress.coerceIn(0, 100) / 100f) },
                modifier = Modifier.fillMaxWidth().height(2.dp),
            )
        }
        HorizontalDivider()
    }
}

internal val NanoBook.librarySubtitle: String
    get() = buildList {
        metadata.author.takeIf(String::isNotBlank)?.let(::add)
        metadata.wordCount.takeIf { it > 0 }?.let { add("$it words") }
        if (!isArticle) metadata.chapterCount.takeIf { it > 0 }?.let { add("$it chapters") }
        reading?.percent?.let { add("$it% read") }
    }.joinToString(" · ")

@Composable
private fun LibraryBookDialog(
    book: NanoBook,
    onDismiss: () -> Unit,
    onSetPosition: (Int) -> Unit,
    availableFonts: List<NanoFontSummary>,
    globalFontId: String,
    onSetLanguageFonts: (List<NanoLanguageFont>) -> Unit,
) {
    var showPositionDialog by remember(book.id) { mutableStateOf(false) }
    var showLanguageFonts by remember(book.id) { mutableStateOf(false) }
    val metadata = book.metadata
    val reading = book.reading

    if (showLanguageFonts) {
        BookLanguageFontsDialog(
            book = book,
            availableFonts = availableFonts,
            globalFontId = globalFontId,
            onDismiss = { showLanguageFonts = false },
            onSave = onSetLanguageFonts,
        )
    } else if (!showPositionDialog) {
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
                    metadata.author.takeIf { it.isNotBlank() }?.let { MetadataField("Author", it) }
                    reading?.currentChapter?.let { MetadataField("Current chapter", "${it.number}. ${it.title}") }
                    MetadataField("Word count", metadata.wordCount.toString())
                    MetadataField("Chapters", metadata.chapterCount.toString())
                    metadata.locale.takeIf { it.isNotBlank() }?.let { MetadataField("Language", it) }
                    metadata.scripts.takeIf { it.isNotEmpty() }?.let { MetadataField("Scripts", it.joinToString()) }
                    metadata.direction.takeIf { it != "auto" }?.let { MetadataField("Direction", it.uppercase()) }
                    metadata.requiredCapabilities.takeIf { it.isNotEmpty() }
                        ?.let { MetadataField("Language support", it.joinToString()) }
                    reading?.let {
                        MetadataField("Progress", "${it.percent}%")
                        MetadataField("Remaining", "${it.remainingWords} words")
                        MetadataField("Reading time", "About ${it.estimatedMinutes} min remaining")
                    }
                    MetadataField("File size", book.byteLabel)
                    MetadataField("Filename", book.name)
                    if (book.source != null && metadata.wordCount > 0) {
                        FilledTonalButton(onClick = { showLanguageFonts = true }) {
                            Icon(imageVector = Icons.Outlined.Language, contentDescription = null)
                            Text("Language fonts")
                        }
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
private fun BookLanguageFontsDialog(
    book: NanoBook,
    availableFonts: List<NanoFontSummary>,
    globalFontId: String,
    onDismiss: () -> Unit,
    onSave: (List<NanoLanguageFont>) -> Unit,
) {
    var selections by remember(book.id) { mutableStateOf(book.reading?.languageFonts.orEmpty()) }
    val languages = book.metadata.languages.ifEmpty {
        listOfNotNull(
            book.metadata.locale.takeIf(String::isNotBlank)?.let {
                NanoBookLanguage(it, book.metadata.scriptMask)
            },
        )
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(imageVector = Icons.Outlined.Language, contentDescription = null) },
        title = { Text("Language fonts") },
        text = {
            LazyColumn(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                items(languages, key = { it.locale }) { bookLanguage ->
                    val locale = bookLanguage.locale
                    val requiredScripts = bookLanguage.scriptMask
                    val compatible = availableFonts.filter { it.usableFor(locale, requiredScripts) }
                    val selectedId = selections.firstOrNull { it.locale == locale }?.fontId
                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text(locale, style = MaterialTheme.typography.titleSmall)
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalArrangement = Arrangement.spacedBy(6.dp),
                        ) {
                            FilterChip(
                                selected = selectedId == null,
                                onClick = { selections = selections.filterNot { it.locale == locale } },
                                label = {
                                    val globalName = availableFonts.firstOrNull { it.id == globalFontId }?.name
                                        ?: globalFontId.ifBlank { "default" }
                                    Text("Global ($globalName)")
                                },
                            )
                            compatible.forEach { font ->
                                FilterChip(
                                    selected = selectedId == font.id,
                                    onClick = {
                                        selections = selections.filterNot { it.locale == locale } +
                                            NanoLanguageFont(locale, font.id)
                                    },
                                    label = { Text(font.name) },
                                )
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            Button(onClick = { onSave(selections) }) { Text("Save") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

@Composable
private fun MetadataField(label: String, value: String) {
    Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
        Text(text = label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(text = value, style = MaterialTheme.typography.bodySmall)
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

val NanoBook.byteLabel: String
    get() = bytes.toByteLabel()

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

