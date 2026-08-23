package com.rsvpnano.web

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.Sync
import androidx.compose.material.icons.outlined.UploadFile
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoFocusTimer
import com.rsvpnano.models.NanoFocusTimerRules
import com.rsvpnano.models.NanoFocusTimers
import com.rsvpnano.models.NanoLocales
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoSettingsSchema
import com.rsvpnano.models.needsArticleFetch
import com.rsvpnano.ui.CompanionPresenter
import com.rsvpnano.ui.CatalogInstall
import com.rsvpnano.ui.CatalogAsset
import com.rsvpnano.ui.CompanionResource
import com.rsvpnano.ui.CompanionUiState
import com.rsvpnano.ui.TypographyPreview
import com.rsvpnano.ui.fontDetails
import com.rsvpnano.ui.localeDetails
import io.github.vinceglb.filekit.dialogs.FileKitType
import io.github.vinceglb.filekit.dialogs.compose.rememberFilePickerLauncher
import io.github.vinceglb.filekit.name
import io.github.vinceglb.filekit.readBytes
import kotlinx.coroutines.launch

@Composable
internal fun LibraryWorkspace(presenter: CompanionPresenter, state: CompanionUiState) {
    val scope = rememberCoroutineScope()
    val picker = rememberFilePickerLauncher(
        type = FileKitType.File(extensions = listOf("epub", "txt", "html", "htm", "rsvp")),
    ) { file ->
        if (file != null) scope.launch { presenter.uploadSelectedFile(file.name, file.readBytes()) }
    }

    LaunchedEffect(state.isConnected) {
        if (state.isConnected && CompanionResource.Library !in state.loadedResources) presenter.refreshLibrary()
    }

    WorkspaceHeading("Library")
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Button(onClick = { picker.launch() }, enabled = state.isConnected) { Text("Add book") }
        OutlinedButton(onClick = presenter::refreshLibrary, enabled = state.isConnected) { Text("Refresh") }
        if (state.drafts.isNotEmpty()) {
            OutlinedButton(onClick = presenter::syncSavedArticles, enabled = state.isConnected) {
                Text("Sync ${state.drafts.size} drafts")
            }
        }
    }
    state.bookJob?.let { job ->
        SectionCard {
            Text("${job.active.activeLabel} ${job.name}", fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(8.dp))
            val progress = job.progress
            if (progress != null) LinearProgressIndicator(progress = { progress }, Modifier.fillMaxWidth())
            else LinearProgressIndicator(Modifier.fillMaxWidth())
        }
    }
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        if (maxWidth >= 900.dp) {
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                BookList(presenter, state, Modifier.weight(3f))
                DraftEditor(presenter, state, Modifier.weight(2f))
            }
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                BookList(presenter, state)
                DraftEditor(presenter, state)
            }
        }
    }
}

@Composable
private fun BookList(presenter: CompanionPresenter, state: CompanionUiState, modifier: Modifier = Modifier) {
    SectionCard(modifier) {
        Text("ON THE READER", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.secondary)
        if (state.books.isEmpty()) {
            Text(if (state.isConnected) "No books installed." else "Connect a Nano to load its library.")
        }
        state.books.forEach { book -> BookRow(book, presenter) }
    }
}

@Composable
private fun BookRow(book: NanoBook, presenter: CompanionPresenter) {
    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
        Text(book.displayTitle, fontWeight = FontWeight.Bold)
        val detail = buildList {
            book.metadata.author.takeIf(String::isNotBlank)?.let(::add)
            if (book.metadata.wordCount > 0) add("${book.metadata.wordCount} words")
            if (book.bytes > 0) add(formatBytes(book.bytes))
        }.joinToString(" · ")
        if (detail.isNotBlank()) Text(detail, style = MaterialTheme.typography.bodySmall)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { presenter.setBookPosition(book, 0) }) { Text("Reset position") }
            OutlinedButton(onClick = { presenter.deleteDeviceBook(book) }) { Text("Remove") }
        }
    }
}

@Composable
private fun DraftEditor(presenter: CompanionPresenter, state: CompanionUiState, modifier: Modifier = Modifier) {
    SectionCard(modifier) {
        Text("ARTICLE DESK", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.secondary)
        OutlinedTextField(
            value = state.draftTitle,
            onValueChange = presenter::setDraftTitle,
            label = { Text("Title") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        OutlinedTextField(
            value = state.draftSourceUrl,
            onValueChange = presenter::setDraftSourceUrl,
            label = { Text("Article URL (optional)") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        OutlinedTextField(
            value = state.draftBody,
            onValueChange = presenter::setDraftBody,
            label = { Text("Paste article text or HTML") },
            modifier = Modifier.fillMaxWidth().height(150.dp),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = presenter::saveTextDraft) { Text("Save text") }
            OutlinedButton(onClick = presenter::saveLinkDraft, enabled = state.draftSourceUrl.isNotBlank()) {
                Text("Fetch URL")
            }
        }
        if (state.drafts.isNotEmpty()) {
            Text("SAVED LOCALLY", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.secondary)
            state.drafts.forEach { draft ->
                Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                    Text(draft.title, fontWeight = FontWeight.Bold)
                    if (draft.needsArticleFetch()) {
                        Text("The site blocked browser fetching. Edit this draft and paste the article text.", style = MaterialTheme.typography.bodySmall)
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = { presenter.editDraft(draft) }) { Text("Edit") }
                        OutlinedButton(onClick = { presenter.deleteDraft(draft) }) { Text("Delete") }
                    }
                }
            }
        }
    }
}

@Composable
internal fun AppearanceWorkspace(presenter: CompanionPresenter, state: CompanionUiState) {
    val scope = rememberCoroutineScope()
    val themePicker = rememberFilePickerLauncher(FileKitType.File(listOf("toml"))) { file ->
        if (file != null) scope.launch { presenter.uploadThemeFile(file.name, file.readBytes()) }
    }
    val fontPicker = rememberFilePickerLauncher(FileKitType.File(listOf("rfont4"))) { file ->
        if (file != null) scope.launch { presenter.uploadFontFile(file.name, file.readBytes()) }
    }
    val localePicker = rememberFilePickerLauncher(FileKitType.File(listOf("zip"))) { file ->
        if (file != null) scope.launch { presenter.installLocalePackFile(file.name, file.readBytes()) }
    }

    LaunchedEffect(state.isConnected) {
        if (!state.isConnected) return@LaunchedEffect
        presenter.refreshSettings()
        presenter.refreshThemes()
        presenter.refreshFonts()
        presenter.refreshLocales()
        presenter.refreshThemeCatalog()
        presenter.refreshFontCatalog()
        presenter.refreshLocaleCatalog()
    }

    WorkspaceHeading("Appearance")

    val themeEntries = remember(state.availableThemes, state.themeCatalog, state.settings, state.catalogInstall) {
        val installed = state.availableThemes.associateBy { it.id }
        val catalog = state.themeCatalog.associateBy { it.id }
        buildList {
            (listOf(NanoSettingsSchema.THEME_DEFAULT) + catalog.keys + installed.keys).distinct().forEach { id ->
                val local = installed[id]
                val online = catalog[id]
                val isInstalled = id == NanoSettingsSchema.THEME_DEFAULT || local != null
                add(CatalogEntry(
                    id = id,
                    title = local?.name ?: online?.name ?: "Default",
                    subtitle = if (id == NanoSettingsSchema.THEME_DEFAULT) "Built in" else if (isInstalled) "Installed" else "Available to install",
                    selected = isInstalled && state.settings?.`interface`?.selectedThemeId == id,
                    install = state.catalogInstall?.takeIf { it.asset == CatalogAsset.Theme && it.id == id },
                    onSelect = if (isInstalled) ({ presenter.selectTheme(id) }) else null,
                    onDelete = if (local != null && id != NanoSettingsSchema.THEME_DEFAULT) ({ presenter.removeTheme(id) }) else null,
                    onInstall = if (!isInstalled && online != null) ({ presenter.installOnlineTheme(id) }) else null,
                ))
            }
        }
    }
    val fontEntries = remember(state.availableFonts, state.fontCatalog, state.settings, state.catalogInstall) {
        val installed = state.availableFonts.associateBy { it.id }
        val catalog = state.fontCatalog.associateBy { it.id }
        (catalog.keys + installed.keys).distinct().map { id ->
            val local = installed[id]
            val online = catalog[id]
            CatalogEntry(
                id = id,
                title = local?.name ?: online?.name ?: id,
                subtitle = local?.let { fontDetails(it.scripts, it.builtIn, false) }
                    ?: online?.let { fontDetails(it.scripts, false, it.shaping) }.orEmpty(),
                selected = local != null && state.settings?.reading?.typography?.fontId == id,
                install = state.catalogInstall?.takeIf { it.asset == CatalogAsset.Font && it.id == id },
                onSelect = local?.let { { presenter.selectFont(id) } },
                onDelete = local?.takeIf { !it.builtIn }?.let { { presenter.removeFont(id) } },
                onInstall = online?.takeIf { local == null }?.let { { presenter.installOnlineFont(id) } },
            )
        }
    }
    val localeEntries = remember(state.availableLocales, state.localeCatalog, state.settings, state.catalogInstall) {
        val installed = state.availableLocales.associateBy { it.id }
        val catalog = state.localeCatalog.associateBy { it.id }
        buildList {
            add(CatalogEntry(
                id = NanoLocales.DEFAULT,
                title = "English",
                subtitle = "Built in · Left-to-right",
                selected = state.settings?.`interface`?.locale == NanoLocales.DEFAULT,
                onSelect = { presenter.selectLocale(NanoLocales.DEFAULT) },
            ))
            (catalog.keys + installed.keys).distinct().filterNot { it == NanoLocales.DEFAULT }.forEach { id ->
                val local = installed[id]
                val online = catalog[id]
                add(CatalogEntry(
                    id = id,
                    title = local?.name ?: online?.name ?: id,
                    subtitle = online?.let { localeDetails(it.englishName, it.direction, it.translationStatus, it.version) }
                        ?: local?.locale.orEmpty(),
                    selected = local != null && state.settings?.`interface`?.locale == local.locale,
                    install = state.catalogInstall?.takeIf { it.asset == CatalogAsset.Locale && it.id == id },
                    onSelect = local?.let { { presenter.selectLocale(it.locale) } },
                    onDelete = local?.let { { presenter.removeLocalePack(id) } },
                    onInstall = online?.takeIf { local == null }?.let { { presenter.installOnlineLocalePack(id) } },
                ))
            }
        }
    }

    var selectedCatalog by remember { mutableStateOf(0) }
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        val catalogs = listOf<@Composable () -> Unit>(
            { CatalogColumn("Themes", state.themeCatalogUrl, themeEntries, presenter::refreshThemeCatalog) { themePicker.launch() } },
            { CatalogColumn("Reader fonts", state.fontCatalogUrl, fontEntries, presenter::refreshFontCatalog) { fontPicker.launch() } },
            { CatalogColumn("Interface languages", state.localeCatalogUrl, localeEntries, presenter::refreshLocaleCatalog) { localePicker.launch() } },
        )
        if (maxWidth >= 960.dp) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                catalogs.forEach { catalog -> Column(Modifier.weight(1f)) { catalog() } }
            }
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    listOf("Themes", "Fonts", "Languages").forEachIndexed { index, label ->
                        val active = selectedCatalog == index
                        val shape = RoundedCornerShape(14.dp)
                        Surface(
                            modifier = Modifier.weight(1f).clip(shape).clickable { selectedCatalog = index },
                            shape = shape,
                            color = if (active) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surface,
                            contentColor = if (active) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface,
                        ) {
                            Text(label, Modifier.padding(vertical = 10.dp), textAlign = androidx.compose.ui.text.style.TextAlign.Center)
                        }
                    }
                }
                catalogs[selectedCatalog]()
            }
        }
    }
}

private data class CatalogEntry(
    val id: String,
    val title: String,
    val subtitle: String,
    val selected: Boolean = false,
    val install: CatalogInstall? = null,
    val onSelect: (() -> Unit)? = null,
    val onDelete: (() -> Unit)? = null,
    val onInstall: (() -> Unit)? = null,
)

@Composable
private fun CatalogColumn(
    title: String,
    source: String,
    entries: List<CatalogEntry>,
    onRefresh: () -> Unit,
    onUpload: () -> Unit,
) {
    SectionCard {
        Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(catalogSourceLabel(source), Modifier.weight(1f), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.62f))
            IconButton(onClick = onRefresh) { Icon(Icons.Outlined.Sync, "Refresh $title") }
            OutlinedButton(onClick = onUpload) {
                Icon(Icons.Outlined.UploadFile, null, Modifier.size(18.dp))
                Spacer(Modifier.width(5.dp))
                Text("Upload")
            }
        }
        if (entries.isEmpty()) Text("Connect a Nano to load this catalog.")
        entries.forEach { entry -> CatalogRow(entry) }
    }
}

@Composable
private fun CatalogRow(entry: CatalogEntry) {
    val shape = RoundedCornerShape(12.dp)
    Surface(
        modifier = Modifier.fillMaxWidth().clip(shape)
            .border(1.dp, if (entry.selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline.copy(alpha = 0.22f), shape)
            .then(if (entry.onSelect == null) Modifier else Modifier.clickable(onClick = entry.onSelect)),
        shape = shape,
        color = if (entry.selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.1f) else MaterialTheme.colorScheme.surface,
    ) {
        Column {
            Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(entry.title, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Bold)
                    if (entry.selected) Text("Default", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                    if (entry.subtitle.isNotBlank()) Text(entry.subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f))
                }
                when {
                    entry.onDelete != null -> IconButton(onClick = entry.onDelete) {
                        Icon(Icons.Outlined.Delete, "Remove ${entry.title}", tint = MaterialTheme.colorScheme.error)
                    }
                    entry.onInstall != null -> TextButton(onClick = entry.onInstall) { Text("Install") }
                }
            }
            entry.install?.let { job ->
                Text(job.stage.label, Modifier.padding(horizontal = 12.dp), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.primary)
                val progress = job.progress
                if (progress == null) LinearProgressIndicator(Modifier.fillMaxWidth())
                else LinearProgressIndicator(progress = { progress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth())
            }
        }
    }
}

private fun catalogSourceLabel(url: String): String {
    if (url.isBlank()) return "Catalog"
    val githubPath = url.substringAfter("githubusercontent.com/", "")
    return if (githubPath.isNotBlank()) githubPath.split('/').take(3).joinToString("/")
    else url.substringAfter("://").substringBefore('/')
}

@Composable
internal fun SettingsWorkspace(presenter: CompanionPresenter, state: CompanionUiState) {
    LaunchedEffect(state.isConnected) {
        if (!state.isConnected) return@LaunchedEffect
        presenter.refreshSettings()
        presenter.refreshWifiSettings()
    }

    WorkspaceHeading("Settings")
    val settings = state.settings
    if (settings == null) {
        Text(if (state.isConnected) "Loading settings…" else "Connect a Nano to edit settings.")
        return
    }

    ReadingSettings(presenter, settings)
    DisplaySettings(presenter, settings)
    UpdateSettings(presenter, state, settings)
    NetworkSettings(presenter, state)
}

@Composable
private fun ReadingSettings(presenter: CompanionPresenter, settings: NanoSettings) {
    val reading = settings.reading
    SectionCard {
        Text("Reading", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        TypographyPreview(
            typography = reading.typography,
            phantomWords = reading.phantomWords,
            modifier = Modifier.fillMaxWidth().height(180.dp),
        )
        IntSlider("Words per minute", reading.wpm, NanoSettingsSchema.WPM_MIN..NanoSettingsSchema.WPM_MAX) {
            presenter.updateSettings { current -> current.withWpm(it) }
        }
        ChoiceRow("Mode", reading.mode, listOf("rsvp" to "RSVP", "page" to "Page")) { value ->
            presenter.updateSettings { it.copy(reading = it.reading.copy(mode = value)) }
        }
        ChoiceRow("Pause timing", reading.pauseMode, listOf("sentenceEnd" to "Sentence end", "instant" to "Instant")) { value ->
            presenter.updateSettings { it.withPauseMode(value) }
        }
        ToggleRow("Phantom words", reading.phantomWords) { value -> presenter.updateSettings { it.withPhantomWords(value) } }
        ToggleRow("Reverse chapter scrolling", reading.chapterScrollReversed) { value ->
            presenter.updateSettings { it.copy(reading = it.reading.copy(chapterScrollReversed = value)) }
        }
        ToggleRow("Left-handed controls", reading.leftHanded) { value ->
            presenter.updateSettings { it.withHandedness(if (value) "left" else "right") }
        }
        ToggleRow("Focus highlight", reading.typography.focusHighlight) { value ->
            presenter.updateSettings { it.withFocusHighlight(value) }
        }
        IntSlider("Font size", reading.typography.fontSizeIndex, NanoSettingsSchema.FONT_SIZE_MIN..NanoSettingsSchema.FONT_SIZE_MAX) {
            presenter.updateSettings { current -> current.withFontSizeIndex(it) }
        }
        IntSlider("Tracking", reading.typography.tracking, NanoSettingsSchema.TRACKING_MIN..NanoSettingsSchema.TRACKING_MAX) {
            presenter.updateSettings { current -> current.withTracking(it) }
        }
        IntSlider("Focus anchor", reading.typography.anchor, NanoSettingsSchema.ANCHOR_PERCENT_MIN..NanoSettingsSchema.ANCHOR_PERCENT_MAX, "%") {
            presenter.updateSettings { current -> current.withAnchorPercent(it) }
        }
        IntSlider("Guide width", reading.typography.guideWidth, NanoSettingsSchema.GUIDE_WIDTH_MIN..NanoSettingsSchema.GUIDE_WIDTH_MAX) {
            presenter.updateSettings { current -> current.withGuideWidth(it) }
        }
        IntSlider("Guide gap", reading.typography.guideGap, NanoSettingsSchema.GUIDE_GAP_MIN..NanoSettingsSchema.GUIDE_GAP_MAX) {
            presenter.updateSettings { current -> current.withGuideGap(it) }
        }
        IntSlider("Long word delay", reading.pacing.longWordDelayMs, NanoSettingsSchema.PACING_MS_MIN..NanoSettingsSchema.PACING_MS_MAX, " ms") {
            presenter.updateSettings { current -> current.withPacingLongWordMs(it) }
        }
        IntSlider("Complex word delay", reading.pacing.complexWordDelayMs, NanoSettingsSchema.PACING_MS_MIN..NanoSettingsSchema.PACING_MS_MAX, " ms") {
            presenter.updateSettings { current -> current.withPacingComplexWordMs(it) }
        }
        IntSlider("Punctuation delay", reading.pacing.punctuationDelayMs, NanoSettingsSchema.PACING_MS_MIN..NanoSettingsSchema.PACING_MS_MAX, " ms") {
            presenter.updateSettings { current -> current.withPacingPunctuationMs(it) }
        }
        ChoiceRow("Footer", reading.footerMetric, listOf("percentage" to "Percentage", "chapterTime" to "Chapter time", "bookTime" to "Book time")) { value ->
            presenter.updateSettings { it.withFooterMetric(value) }
        }
        ChoiceRow("Battery label", reading.batteryLabel, listOf("percentage" to "Percentage", "timeRemaining" to "Time left", "voltage" to "Voltage")) { value ->
            presenter.updateSettings { it.withBatteryLabel(value) }
        }
        ToggleRow("Battery icon", reading.batteryIconVisible) { value -> presenter.updateSettings { it.withBatteryIconVisible(value) } }
        ToggleRow("Battery while reading", reading.batteryVisibleWhileReading) { value -> presenter.updateSettings { it.withReadingBattery(value) } }
        ToggleRow("Chapter while reading", reading.chapterVisibleWhileReading) { value -> presenter.updateSettings { it.withReadingChapter(value) } }
        ToggleRow("Progress while reading", reading.progressVisibleWhileReading) { value -> presenter.updateSettings { it.withReadingProgress(value) } }
    }
}

@Composable
private fun DisplaySettings(presenter: CompanionPresenter, settings: NanoSettings) {
    val display = settings.`interface`
    SectionCard {
        Text("Display", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        IntSlider("Brightness", display.brightnessPercent, NanoSettingsSchema.BRIGHTNESS_MIN..NanoSettingsSchema.BRIGHTNESS_MAX, "%") {
            presenter.updateSettings { current -> current.withBrightnessPercent(it) }
        }
        ChoiceRow("Standby", display.standbyTimerIndex.toString(), listOf("0" to "Never", "1" to "1 minute", "2" to "5 minutes", "3" to "15 minutes", "4" to "30 minutes")) { value ->
            presenter.updateSettings { it.withStandbyTimerIndex(value.toInt()) }
        }
        ChoiceRow("Screensaver", display.screensaver, listOf("life" to "Life", "maze" to "Maze", "voronoi" to "Voronoi", "reaction" to "Reaction", "screenOff" to "Screen off")) { value ->
            presenter.updateSettings { it.withScreensaver(value) }
        }
    }
}

@Composable
private fun UpdateSettings(presenter: CompanionPresenter, state: CompanionUiState, settings: NanoSettings) {
    SectionCard {
        Text("Updates", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        ToggleRow("Check on reader startup", settings.updates.checkOnStartup) { value ->
            presenter.updateSettings { it.withUpdateChecksOnStartup(value) }
        }
        ToggleRow("Browser update notifications", state.firmwareNotificationsEnabled, presenter::setFirmwareNotificationsEnabled)
        OutlinedTextField(
            value = settings.updates.repositoryOwner,
            onValueChange = { value -> presenter.updateSettings { it.withUpdateOwner(value) } },
            label = { Text("Repository owner or owner/repository") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        OutlinedTextField(
            value = settings.updates.releaseTag,
            onValueChange = { value -> presenter.updateSettings { it.withUpdateTag(value) } },
            label = { Text("Release tag") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
    }
}

@Composable
private fun NetworkSettings(presenter: CompanionPresenter, state: CompanionUiState) {
    SectionCard {
        Text("Nano Wi-Fi", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        Text("Saved network: ${state.wifiSettings?.ssid?.ifBlank { "none" } ?: "loading…"}")
        OutlinedTextField(state.wifiSsidDraft, presenter::setWifiSsidDraft, Modifier.fillMaxWidth(), label = { Text("SSID") }, singleLine = true)
        OutlinedTextField(state.wifiPasswordDraft, presenter::setWifiPasswordDraft, Modifier.fillMaxWidth(), label = { Text("Password") }, singleLine = true)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = presenter::saveWifiSettings) { Text("Save Wi-Fi") }
            OutlinedButton(onClick = presenter::clearWifiSettings) { Text("Forget network") }
        }
    }
}

@Composable
internal fun FeedsWorkspace(presenter: CompanionPresenter, state: CompanionUiState) {
    LaunchedEffect(state.isConnected) {
        if (!state.isConnected) return@LaunchedEffect
        presenter.refreshRssFeeds()
    }
    WorkspaceHeading("Feeds")
    SectionCard {
        Text("RSS feeds", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(state.rssFeedDraft, presenter::setRssFeedDraft, Modifier.weight(1f), label = { Text("Feed URL") }, singleLine = true)
            Button(onClick = presenter::addRssFeed) { Text("Add") }
        }
        state.rssFeeds.forEach { feed ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(feed, Modifier.weight(1f))
                OutlinedButton(onClick = { presenter.deleteRssFeed(feed) }) { Text("Remove") }
            }
        }
        OutlinedButton(onClick = presenter::refreshRssFeeds) { Text("Fetch latest articles") }
    }
}

@Composable
internal fun TimersWorkspace(presenter: CompanionPresenter, state: CompanionUiState) {
    LaunchedEffect(state.isConnected) {
        if (state.isConnected) presenter.refreshFocusTimers()
    }
    WorkspaceHeading("Timers")
    FocusEditor(presenter, state)
}

@Composable
private fun FocusEditor(presenter: CompanionPresenter, state: CompanionUiState) {
    var timers by remember(state.focusTimers) { mutableStateOf(state.focusTimers.timers) }
    SectionCard {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Text("Focus routines", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
            OutlinedButton(
                onClick = { timers = timers + NanoFocusTimer(name = "Routine ${timers.size + 1}") },
                enabled = timers.size < NanoFocusTimerRules.MAX_TIMERS,
            ) { Text("Add routine") }
        }
        timers.forEachIndexed { index, timer ->
            Column(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    timer.name,
                    { value -> timers = timers.toMutableList().also { it[index] = timer.copy(name = value) } },
                    Modifier.fillMaxWidth(),
                    label = { Text("Routine name") },
                    singleLine = true,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    NumberField("Focus min", timer.focusMinutes, Modifier.weight(1f)) { value ->
                        timers = timers.toMutableList().also { it[index] = timer.copy(focusMinutes = value) }
                    }
                    NumberField("Break min", timer.breakMinutes, Modifier.weight(1f)) { value ->
                        timers = timers.toMutableList().also { it[index] = timer.copy(breakMinutes = value) }
                    }
                    NumberField("Rounds", timer.rounds, Modifier.weight(1f)) { value ->
                        timers = timers.toMutableList().also { it[index] = timer.copy(rounds = value) }
                    }
                }
                OutlinedButton(onClick = { timers = timers.filterIndexed { itemIndex, _ -> itemIndex != index } }) { Text("Remove routine") }
            }
        }
        Button(
            onClick = { presenter.saveFocusTimers(NanoFocusTimers(timers)) },
            enabled = timers.all(NanoFocusTimerRules::valid),
        ) { Text("Save routines") }
    }
}

@Composable
private fun IntSlider(label: String, value: Int, range: IntRange, suffix: String = "", onChange: (Int) -> Unit) {
    Column {
        DetailRow(label, "$value$suffix")
        Slider(
            value = value.toFloat(),
            onValueChange = { onChange(it.toInt()) },
            valueRange = range.first.toFloat()..range.last.toFloat(),
        )
    }
}

@Composable
private fun ChoiceRow(label: String, selected: String, choices: List<Pair<String, String>>, onSelect: (String) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(label, fontWeight = FontWeight.Bold)
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            choices.forEach { (value, text) -> FilterChip(selected == value, { onSelect(value) }, { Text(text) }) }
        }
    }
}

@Composable
private fun ToggleRow(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        Switch(checked, onCheckedChange)
    }
}

@Composable
private fun NumberField(label: String, value: Int, modifier: Modifier = Modifier, onChange: (Int) -> Unit) {
    OutlinedTextField(
        value = value.toString(),
        onValueChange = { text -> text.toIntOrNull()?.let(onChange) },
        modifier = modifier,
        label = { Text(label) },
        singleLine = true,
    )
}

@Composable
private fun SectionCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Card(modifier, colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)) {
        Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            content()
        }
    }
}

private fun formatBytes(bytes: Int): String = when {
    bytes >= 1024 * 1024 -> "${bytes / (1024 * 1024)} MB"
    bytes >= 1024 -> "${bytes / 1024} KB"
    else -> "$bytes B"
}
