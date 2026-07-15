package com.rsvpnano.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.HelpOutline
import androidx.compose.material.icons.automirrored.outlined.LibraryBooks
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.UploadFile
import androidx.compose.material.icons.outlined.Wifi
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FabPosition
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.rsvpnano.app.CompanionNotice
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.models.needsArticleFetch
import io.github.vinceglb.filekit.name
import io.github.vinceglb.filekit.readBytes
import io.github.vinceglb.filekit.dialogs.FileKitType
import io.github.vinceglb.filekit.dialogs.compose.rememberFilePickerLauncher
import kotlinx.coroutines.launch

enum class CompanionTab(val label: String, val icon: ImageVector) {
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
    val colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme()
    MaterialTheme(colorScheme = colorScheme) {
        val snackbarHostState = remember { SnackbarHostState() }
        val snackbarNotices = remember { mutableStateMapOf<String, CompanionNotice>() }
        val scope = rememberCoroutineScope()
        var selectedTab by remember { mutableStateOf(CompanionTab.Library) }
        var showAddPicker by remember { mutableStateOf(false) }
        var showArticleDialog by remember { mutableStateOf(false) }
        var showRssDialog by remember { mutableStateOf(false) }
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
            type = FileKitType.File(extensions = listOf("rtheme")),
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
        }

        Scaffold(
            topBar = {
                Column {
                    TopAppBar(
                        title = { Text(text = "RSVP Nano") },
                        actions = {
                            IconButton(onClick = presenter::showHelpNotice) {
                                Icon(imageVector = Icons.AutoMirrored.Outlined.HelpOutline, contentDescription = "Help")
                            }
                        },
                        colors = TopAppBarDefaults.topAppBarColors(
                            containerColor = MaterialTheme.colorScheme.background,
                        ),
                    )
                    SharedConnectionBar(
                        uiState = uiState,
                        onRememberCurrentNano = presenter::rememberCurrentNano,
                    )
                }
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
                ExtendedFloatingActionButton(
                    onClick = onConnect,
                    icon = { Icon(imageVector = Icons.Outlined.Wifi, contentDescription = null) },
                    text = { Text(if (uiState.isConnected) "Reconnect" else "Connect") },
                )
            },
            floatingActionButtonPosition = FabPosition.End,
            bottomBar = {
                NavigationBar {
                    CompanionTab.entries.forEach { tab ->
                        NavigationBarItem(
                            selected = selectedTab == tab,
                            onClick = { selectedTab = tab },
                            icon = { Icon(imageVector = tab.icon, contentDescription = null) },
                            label = { Text(text = tab.label) },
                        )
                    }
                }
            },
        ) { contentPadding ->
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(contentPadding)
                    .padding(horizontal = 20.dp, vertical = 12.dp),
            ) {
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
                        onShowUpload = { showAddPicker = true },
                    )

                    CompanionTab.Settings -> SettingsTab(
                        uiState = uiState,
                        presenter = presenter,
                        onFirmwareNotificationsChange = onFirmwareNotificationsChange,
                        hasPermissions = hasPermissions,
                        onGrantPermissions = onGrantPermissions,
                        onUploadTheme = { themePicker.launch() },
                        onUploadFont = { fontPicker.launch() },
                    )
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
        }
    }
}

@Composable
private fun SharedConnectionBar(
    uiState: CompanionUiState,
    onRememberCurrentNano: () -> Unit,
) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceVariant,
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(
                    imageVector = if (uiState.isConnected) Icons.Outlined.CheckCircle else Icons.Outlined.Wifi,
                    contentDescription = null,
                )
                Text(
                    text = uiState.status,
                    modifier = Modifier.weight(1f),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodyMedium,
                )
                if (uiState.isCheckingReader || uiState.isRefreshing) {
                    CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                }
            }
            if (uiState.canRememberCurrentNano) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        text = "Remember ${uiState.nanoSsid} for direct connection when regular Wi-Fi is unavailable?",
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.bodySmall,
                    )
                    TextButton(onClick = onRememberCurrentNano) {
                        Text("Remember")
                    }
                }
            }
        }
    }
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
