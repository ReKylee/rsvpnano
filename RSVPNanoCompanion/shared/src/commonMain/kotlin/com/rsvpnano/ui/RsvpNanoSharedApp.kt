package com.rsvpnano.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.HelpOutline
import androidx.compose.material.icons.automirrored.outlined.LibraryBooks
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.UploadFile
import androidx.compose.material.icons.outlined.Wifi
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FabPosition
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.rsvpnano.app.CompanionNotice
import com.rsvpnano.app.NanoConnectionTransport
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.models.needsArticleFetch
import io.github.vinceglb.filekit.name
import io.github.vinceglb.filekit.readBytes
import io.github.vinceglb.filekit.dialogs.FileKitType
import io.github.vinceglb.filekit.dialogs.compose.rememberFilePickerLauncher
import kotlinx.coroutines.launch

private enum class CompanionTab(val label: String, val icon: ImageVector) {
    Library("Library", Icons.AutoMirrored.Outlined.LibraryBooks),
    Settings("Settings", Icons.Outlined.Settings),
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RsvpNanoSharedApp(
    uiState: CompanionUiState,
    presenter: CompanionPresenter,
    hasPermissions: Boolean,
    onConnect: () -> Unit,
    onFirmwareNotificationsChange: (Boolean) -> Unit,
    onGrantPermissions: () -> Unit,
) {
    RsvpNanoTheme {
        val snackbarHostState = remember { SnackbarHostState() }
        val snackbarNotices = remember { mutableStateMapOf<String, CompanionNotice>() }
        val scope = rememberCoroutineScope()
        var selectedTabName by rememberSaveable { mutableStateOf(CompanionTab.Library.name) }
        val selectedTab = CompanionTab.valueOf(selectedTabName)
        var showAddPicker by rememberSaveable { mutableStateOf(false) }
        var showArticleDialog by rememberSaveable { mutableStateOf(false) }
        var showRssDialog by rememberSaveable { mutableStateOf(false) }
        var showConnectionDialog by rememberSaveable { mutableStateOf(false) }
        var showHelpDialog by rememberSaveable { mutableStateOf(false) }
        var settingsHelpTitle by rememberSaveable { mutableStateOf("Settings") }
        var settingsHelpBody by rememberSaveable {
            mutableStateOf("Choose a section to configure your reader, its display, languages, or fonts.")
        }
        val filePicker = rememberFilePickerLauncher(
            type = FileKitType.File(extensions = listOf("epub", "txt", "html", "htm", "rsvp")),
        ) { file ->
            if (file != null) {
                scope.launch {
                    presenter.uploadSelectedFile(file.name, file.readBytes())
                }
            }
        }
        val themePicker = rememberFilePickerLauncher(
            type = FileKitType.File(extensions = listOf("toml")),
        ) { file ->
            if (file != null) {
                scope.launch {
                    presenter.uploadThemeFile(file.name, file.readBytes())
                }
            }
        }
        val fontPicker = rememberFilePickerLauncher(
            type = FileKitType.File(extensions = listOf("rfont4")),
        ) { file ->
            if (file != null) {
                scope.launch {
                    presenter.uploadFontFile(file.name, file.readBytes())
                }
            }
        }
        val localePackPicker = rememberFilePickerLauncher(
            type = FileKitType.File(extensions = listOf("zip")),
        ) { file ->
            if (file != null) {
                scope.launch {
                    presenter.installLocalePackFile(file.name, file.readBytes())
                }
            }
        }

        LaunchedEffect(uiState.notice) {
            if (uiState.notice.showTransient) {
                snackbarNotices[uiState.status] = uiState.notice
                snackbarHostState.showSnackbar(uiState.status)
            }
        }

        LaunchedEffect(selectedTab) {
            if (selectedTab == CompanionTab.Settings && uiState.themeCatalog.isEmpty()) {
                presenter.refreshThemeCatalog()
            }
            if (selectedTab == CompanionTab.Settings && uiState.fontCatalog.isEmpty()) {
                presenter.refreshFontCatalog()
            }
            if (selectedTab == CompanionTab.Settings && uiState.localeCatalog.isEmpty()) {
                presenter.refreshLocaleCatalog()
            }
        }

        BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
            val wide = maxWidth >= 840.dp
            Row(modifier = Modifier.fillMaxSize()) {
                if (wide) {
                    NavigationRail {
                        CompanionTab.entries.forEach { tab ->
                            NavigationRailItem(
                                selected = selectedTab == tab,
                                onClick = { selectedTabName = tab.name },
                                icon = { Icon(imageVector = tab.icon, contentDescription = null) },
                                label = { Text(tab.label) },
                            )
                        }
                    }
                }
                Scaffold(
                    modifier = Modifier.weight(1f),
            topBar = {
                TopAppBar(
                    title = { Text(selectedTab.label) },
                    navigationIcon = {
                        if (!wide && selectedTab == CompanionTab.Settings) {
                            IconButton(onClick = { selectedTabName = CompanionTab.Library.name }) {
                                Icon(Icons.AutoMirrored.Outlined.ArrowBack, contentDescription = "Back to library")
                            }
                        }
                    },
                    actions = {
                        if (!wide && selectedTab == CompanionTab.Library) {
                            IconButton(onClick = { selectedTabName = CompanionTab.Settings.name }) {
                                Icon(Icons.Outlined.Settings, contentDescription = "Settings")
                            }
                        }
                        ConnectionButton(
                            uiState = uiState,
                            onConnect = onConnect,
                            onOpenControls = { showConnectionDialog = true },
                        )
                        IconButton(onClick = { showHelpDialog = true }) {
                            Icon(Icons.AutoMirrored.Outlined.HelpOutline, contentDescription = "Help")
                        }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
                )
            },
            snackbarHost = {
                val bookJob = uiState.bookJob
                if (bookJob != null) {
                    BookJobSnackbar(bookJob)
                } else {
                    SnackbarHost(hostState = snackbarHostState) { data ->
                        val notice = snackbarNotices[data.visuals.message]
                            ?: CompanionNotice.Neutral(data.visuals.message)
                        Snackbar(
                            snackbarData = data,
                            containerColor = snackbarColor(notice),
                            contentColor = snackbarContentColor(notice),
                            actionColor = snackbarActionColor(notice),
                        )
                    }
                }
            },
            floatingActionButton = {
                if (selectedTab == CompanionTab.Library) {
                    ExtendedFloatingActionButton(
                        onClick = { showAddPicker = true },
                        icon = { Icon(Icons.Outlined.Add, contentDescription = null) },
                        text = { Text("Add content") },
                    )
                }
            },
            floatingActionButtonPosition = FabPosition.End,
        ) { contentPadding ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(contentPadding)
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                contentAlignment = Alignment.TopCenter,
            ) {
                Box(modifier = Modifier.fillMaxSize().widthIn(max = 840.dp)) {
                    when (selectedTab) {
                    CompanionTab.Library -> LibraryTab(
                        uiState = uiState,
                        onRefresh = presenter::refresh,
                        needsArticleFetch = PendingUpload::needsArticleFetch,
                        onEditDraft = {
                            presenter.editDraft(it)
                            showArticleDialog = true
                        },
                        onDeleteDraft = presenter::deleteDraft,
                        onSyncArticles = presenter::syncSavedArticles,
                        onDeleteBook = presenter::deleteDeviceBook,
                        onSetBookPosition = presenter::setBookPosition,
                        onSetBookLanguageFonts = presenter::setBookLanguageFonts,
                        onAddContent = { showAddPicker = true },
                    )

                    CompanionTab.Settings -> SettingsTab(
                        uiState = uiState,
                        presenter = presenter,
                        onFirmwareNotificationsChange = onFirmwareNotificationsChange,
                        hasPermissions = hasPermissions,
                        onGrantPermissions = onGrantPermissions,
                        onUploadTheme = { themePicker.launch() },
                        onUploadFont = { fontPicker.launch() },
                        onUploadLocalePack = { localePackPicker.launch() },
                        onHelpChanged = { title, body ->
                            settingsHelpTitle = title
                            settingsHelpBody = body
                        },
                    )
                }
                }
            }

            if (showAddPicker) {
                AddContentDialog(
                    onDismiss = { showAddPicker = false },
                    onUploadBook = {
                        showAddPicker = false
                        filePicker.launch()
                    },
                    onAddArticle = {
                        showAddPicker = false
                        showArticleDialog = true
                    },
                    onAddRssFeed = {
                        showAddPicker = false
                        showRssDialog = true
                    },
                )
            }

            if (uiState.discoveredNanos.isNotEmpty()) {
                NanoPickerDialog(
                    nanos = uiState.discoveredNanos,
                    onSelect = presenter::selectDiscoveredNano,
                    onDismiss = presenter::cancelNanoSelection,
                )
            }

            if (showArticleDialog) {
                AddArticleDialog(
                    uiState = uiState,
                    onDismiss = {
                        showArticleDialog = false
                        presenter.cancelDraftEdit()
                    },
                    onTitleChange = presenter::setDraftTitle,
                    onSourceChange = presenter::setDraftSourceUrl,
                    onBodyChange = presenter::setDraftBody,
                    onSaveText = {
                        showArticleDialog = false
                        presenter.saveTextDraft()
                    },
                    onSaveLink = {
                        showArticleDialog = false
                        presenter.saveLinkDraft()
                    },
                )
            }

            if (showRssDialog) {
                RssFeedsDialog(
                    uiState = uiState,
                    onDismiss = { showRssDialog = false },
                    onFeedChange = presenter::setRssFeedDraft,
                    onAddFeed = presenter::addRssFeed,
                    onRefreshFeeds = presenter::refreshRssFeeds,
                    onDeleteFeed = presenter::deleteRssFeed,
                )
            }

            if (showConnectionDialog) {
                ConnectionDialog(
                    uiState = uiState,
                    onDismiss = { showConnectionDialog = false },
                    onReconnect = {
                        showConnectionDialog = false
                        onConnect()
                    },
                    onRememberCurrentNano = presenter::rememberCurrentNano,
                )
            }

            if (showHelpDialog) {
                val help = if (selectedTab == CompanionTab.Library) {
                    "Library" to "Add books, saved articles, or RSS feeds here. Connect to sync them with your reader."
                } else {
                    settingsHelpTitle to settingsHelpBody
                }
                HelpDialog(
                    title = help.first,
                    body = help.second,
                    onDismiss = { showHelpDialog = false },
                )
            }
        }
            }
        }
    }
}

@Composable
private fun ConnectionButton(
    uiState: CompanionUiState,
    onConnect: () -> Unit,
    onOpenControls: () -> Unit,
) {
    val busy = uiState.isCheckingReader || uiState.isRequestingNanoNetwork
    TextButton(
        onClick = if (uiState.isConnected) onOpenControls else onConnect,
        enabled = !busy,
    ) {
        if (busy) {
            CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
        } else if (uiState.isConnected) {
            ConnectionDot()
        } else {
            Icon(Icons.Outlined.Wifi, contentDescription = null)
        }
        Text(
            text = when {
                busy -> "Connecting"
                uiState.isConnected -> uiState.currentNano?.ssid ?: "Nano"
                else -> "Connect"
            },
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun ConnectionDot() {
    Box(
        Modifier
            .padding(end = 6.dp)
            .size(8.dp)
            .clip(MaterialTheme.shapes.extraSmall)
            .background(Color(0xFF3C8C69)),
    )
}

@Composable
private fun ConnectionDialog(
    uiState: CompanionUiState,
    onDismiss: () -> Unit,
    onReconnect: () -> Unit,
    onRememberCurrentNano: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Outlined.CheckCircle, contentDescription = null) },
        title = { Text(uiState.currentNano?.ssid ?: "RSVP Nano") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Connected", style = MaterialTheme.typography.titleSmall)
                Text(
                    when (uiState.connectionState.transport) {
                        NanoConnectionTransport.LocalNetwork -> "Using the local network"
                        NanoConnectionTransport.AccessPoint -> "Using the Nano's direct Wi-Fi"
                        null -> uiState.baseUrl
                    },
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (uiState.canRememberCurrentNano) {
                    TextButton(onClick = onRememberCurrentNano) {
                        Text("Remember this Nano")
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onReconnect) { Text("Reconnect") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
}

@Composable
private fun HelpDialog(title: String, body: String, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.AutoMirrored.Outlined.HelpOutline, contentDescription = null) },
        title = { Text("$title help") },
        text = { Text(body) },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Got it") } },
    )
}

@Composable
private fun NanoPickerDialog(
    nanos: List<NanoEndpoint>,
    onSelect: (NanoEndpoint) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Choose a Nano") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                nanos.forEach { endpoint ->
                    TextButton(
                        onClick = { onSelect(endpoint) },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(endpoint.nano.ssid, modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        },
    )
}

@Composable
private fun BookJobSnackbar(job: BookJob) {
    val progress = job.progress
    val percent = job.percent
    Snackbar(
        modifier = Modifier.padding(12.dp),
        containerColor = MaterialTheme.colorScheme.inverseSurface,
        contentColor = MaterialTheme.colorScheme.inverseOnSurface,
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            job.done.forEach { step ->
                Row(
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Icon(imageVector = Icons.Outlined.CheckCircle, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(
                        text = "${step.doneLabel} \"${job.name}\"",
                        modifier = Modifier.weight(1f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
            Row(
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                if (progress == null) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(18.dp),
                        strokeWidth = 2.dp,
                        color = MaterialTheme.colorScheme.inverseOnSurface,
                        trackColor = MaterialTheme.colorScheme.inverseSurface,
                    )
                } else {
                    Icon(imageVector = Icons.Outlined.UploadFile, contentDescription = null, modifier = Modifier.size(20.dp))
                }
                Text(
                    text = buildString {
                        append(job.active.activeLabel)
                        append(" \"")
                        append(job.name)
                        append("\"")
                        if (percent != null) {
                            append(" ")
                            append(percent)
                            append("%")
                        }
                    },
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
            if (progress != null) {
                LinearProgressIndicator(
                    progress = { progress.coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth(),
                    color = MaterialTheme.colorScheme.inverseOnSurface,
                    trackColor = MaterialTheme.colorScheme.inverseSurface.copy(alpha = 0.32f),
                )
            }
        }
    }
}

private fun snackbarColor(notice: CompanionNotice): Color =
    when (notice) {
        is CompanionNotice.Success -> Color(0xFF0F5F3D)
        is CompanionNotice.Attention -> Color(0xFF705100)
        is CompanionNotice.Error -> Color(0xFF8C1D18)
        is CompanionNotice.Neutral -> Color(0xFF1F2933)
    }

private fun snackbarContentColor(notice: CompanionNotice): Color =
    when (notice) {
        is CompanionNotice.Attention -> Color(0xFFFFF4CC)
        else -> Color.White
    }

private fun snackbarActionColor(notice: CompanionNotice): Color =
    when (notice) {
        is CompanionNotice.Attention -> Color(0xFFFFD766)
        else -> Color(0xFFB8E6FF)
    }
