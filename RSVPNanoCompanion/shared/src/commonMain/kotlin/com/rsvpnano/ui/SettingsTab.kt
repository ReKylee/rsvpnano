package com.rsvpnano.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.MenuBook
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.CloudUpload
import androidx.compose.material.icons.outlined.Palette
import androidx.compose.material.icons.outlined.Sync
import androidx.compose.material.icons.outlined.SystemUpdate
import androidx.compose.material.icons.outlined.TextFields
import androidx.compose.material.icons.outlined.UploadFile
import androidx.compose.material.icons.outlined.WarningAmber
import androidx.compose.material.icons.outlined.Wifi
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.rsvpnano.models.NanoFont
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoSettingsSchema
import com.rsvpnano.models.NanoTheme

private enum class SettingsCategory(
    val label: String,
    val icon: ImageVector,
) {
    Device("Device", Icons.Outlined.Wifi),
    Reading("Reading", Icons.AutoMirrored.Outlined.MenuBook),
    Display("Display", Icons.Outlined.Palette),
    Typography("Typography", Icons.Outlined.TextFields),
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsTab(
    uiState: CompanionUiState,
    presenter: CompanionPresenter,
    onFirmwareNotificationsChange: (Boolean) -> Unit,
    hasPermissions: Boolean,
    onGrantPermissions: () -> Unit,
    onUploadTheme: () -> Unit,
    onUploadFont: () -> Unit,
) {
    var category by remember { mutableStateOf(SettingsCategory.Device) }
    val content: @Composable (Modifier) -> Unit = { modifier ->
        SettingsContent(
            category = category,
            uiState = uiState,
            presenter = presenter,
            onFirmwareNotificationsChange = onFirmwareNotificationsChange,
            hasPermissions = hasPermissions,
            onGrantPermissions = onGrantPermissions,
            onUploadTheme = onUploadTheme,
            onUploadFont = onUploadFont,
            modifier = modifier,
        )
    }

    PullRefreshBox(
        isRefreshing = uiState.isRefreshing,
        onRefresh = presenter::refresh,
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
                if (maxWidth >= 720.dp) {
                    Row(modifier = Modifier.fillMaxSize()) {
                        SettingsRail(
                            selected = category,
                            onSelected = { category = it },
                        )
                        VerticalDivider()
                        content(Modifier.weight(1f))
                    }
                } else {
                    Column(modifier = Modifier.fillMaxSize()) {
                        PrimaryTabRow(selectedTabIndex = category.ordinal) {
                            SettingsCategory.entries.forEach { option ->
                                Tab(
                                    selected = category == option,
                                    onClick = { category = option },
                                    icon = { Icon(imageVector = option.icon, contentDescription = null) },
                                    text = {
                                        Text(
                                            text = option.label,
                                            style = MaterialTheme.typography.labelSmall,
                                            maxLines = 1,
                                            overflow = TextOverflow.Clip,
                                            softWrap = false,
                                        )
                                    },
                                )
                            }
                        }
                        content(Modifier.weight(1f))
                    }
                }
        }
    }
}

@Composable
private fun SettingsRail(
    selected: SettingsCategory,
    onSelected: (SettingsCategory) -> Unit,
) {
    NavigationRail(modifier = Modifier.width(184.dp)) {
        SettingsCategory.entries.forEach { option ->
            NavigationRailItem(
                selected = selected == option,
                onClick = { onSelected(option) },
                icon = { Icon(imageVector = option.icon, contentDescription = null) },
                label = { Text(text = option.label) },
            )
        }
    }
}

@Composable
private fun SettingsContent(
    category: SettingsCategory,
    uiState: CompanionUiState,
    presenter: CompanionPresenter,
    onFirmwareNotificationsChange: (Boolean) -> Unit,
    hasPermissions: Boolean,
    onGrantPermissions: () -> Unit,
    onUploadTheme: () -> Unit,
    onUploadFont: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        when (category) {
            SettingsCategory.Device -> DeviceSettings(
                uiState = uiState,
                onWifiSsidChange = presenter::setWifiSsidDraft,
                onWifiPasswordChange = presenter::setWifiPasswordDraft,
                onSaveWifi = presenter::saveWifiSettings,
                onClearWifi = presenter::clearWifiSettings,
                onForgetRememberedNano = presenter::forgetRememberedNano,
                onUpdateSettings = presenter::updateSettings,
                onFirmwareNotificationsChange = onFirmwareNotificationsChange,
                hasPermissions = hasPermissions,
                onGrantPermissions = onGrantPermissions,
            )

            SettingsCategory.Reading -> ReadingSettings(
                settings = uiState.settings,
                isConnected = uiState.isConnected,
                onUpdateSettings = presenter::updateSettings,
            )

            SettingsCategory.Display -> DisplaySettings(
                uiState = uiState,
                onUpdateSettings = presenter::updateSettings,
                onRefreshThemeCatalog = presenter::refreshThemeCatalog,
                onSelectCatalogTheme = presenter::setSelectedCatalogThemeId,
                onInstallOnlineTheme = presenter::installSelectedOnlineTheme,
                onUploadTheme = onUploadTheme,
            )

            SettingsCategory.Typography -> TypographySettings(
                uiState = uiState,
                onUpdateSettings = presenter::updateSettings,
                onRefreshFontCatalog = presenter::refreshFontCatalog,
                onSelectCatalogFont = presenter::setSelectedCatalogFontId,
                onSelectCatalogFontSize = presenter::setSelectedCatalogFontSize,
                onInstallOnlineFont = presenter::installSelectedOnlineFont,
                onUploadFont = onUploadFont,
            )
        }
    }
}

@Composable
private fun SettingsPage(
    content: @Composable () -> Unit,
) {
    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .widthIn(max = 760.dp),
        contentPadding = PaddingValues(start = 20.dp, top = 8.dp, end = 20.dp, bottom = 24.dp),
    ) {
        item {
            Column {
                content()
            }
        }
    }
}

@Composable
private fun DeviceSettings(
    uiState: CompanionUiState,
    onWifiSsidChange: (String) -> Unit,
    onWifiPasswordChange: (String) -> Unit,
    onSaveWifi: () -> Unit,
    onClearWifi: () -> Unit,
    onForgetRememberedNano: () -> Unit,
    onUpdateSettings: ((NanoSettings) -> NanoSettings) -> Unit,
    onFirmwareNotificationsChange: (Boolean) -> Unit,
    hasPermissions: Boolean,
    onGrantPermissions: () -> Unit,
) {
    SettingsPage {
        SettingsSection(
            title = "Reader",
            subtitle = "The app finds the Nano on shared Wi-Fi, then offers its direct network when needed.",
        ) {
            if (!hasPermissions) {
                SettingsStatusRow(
                    icon = Icons.Outlined.WarningAmber,
                    title = "Wi-Fi permission needed",
                    body = "Allow nearby-network access to find Nano devices.",
                    action = {
                        TextButton(onClick = onGrantPermissions) {
                            Text(text = "Grant")
                        }
                    },
                )
            }

            val remembered = uiState.rememberedNano
            SettingsStatusRow(
                icon = if (remembered != null) Icons.Outlined.CheckCircle else Icons.Outlined.Wifi,
                title = if (remembered != null) "Remembered Nano" else "No Nano remembered",
                body = remembered?.ssid ?: if (uiState.isConnected) {
                    "Use the Remember action above to save this Nano for direct connection."
                } else {
                    "Connect once to remember its direct network for times without regular Wi-Fi."
                },
                action = remembered?.let {
                    {
                        TextButton(onClick = onForgetRememberedNano) {
                            Text(text = "Forget")
                        }
                    }
                },
            )
        }

        if (uiState.settings != null && uiState.isConnected) {
            SettingsSection(
                title = "Internet Wi-Fi",
                subtitle = "Saved on the Nano for RSS and device updates.",
            ) {
                val wifiStatus = uiState.wifiSettings?.let { wifi ->
                    if (wifi.configured) "Saved network: ${wifi.ssid}" else "No saved network"
                } ?: "Reader Wi-Fi settings are not loaded."
                SettingsStatusRow(
                    icon = Icons.Outlined.Wifi,
                    title = "Nano internet network",
                    body = wifiStatus,
                )
                OutlinedTextField(
                    value = uiState.wifiSsidDraft,
                    onValueChange = onWifiSsidChange,
                    label = { Text("Network name") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = uiState.wifiPasswordDraft,
                    onValueChange = onWifiPasswordChange,
                    label = { Text("Password") },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onSaveWifi) {
                        Text(text = "Save Wi-Fi")
                    }
                    FilledTonalButton(
                        onClick = onClearWifi,
                        colors = ButtonDefaults.filledTonalButtonColors(
                            containerColor = MaterialTheme.colorScheme.errorContainer,
                            contentColor = MaterialTheme.colorScheme.onErrorContainer,
                        ),
                    ) {
                        Text(text = "Forget network")
                    }
                }
            }
        }

        SettingsSection(
            title = "Firmware updates",
            subtitle = "Use the Nano's release source and notify when its exact OTA image is available.",
        ) {
            val settings = uiState.settings
            if (settings != null && uiState.isConnected) {
                var ownerDraft by remember(settings.updates.owner) { mutableStateOf(settings.updates.owner) }
                var tagDraft by remember(settings.updates.tag) { mutableStateOf(settings.updates.tag) }
                SettingsStatusRow(
                    icon = Icons.Outlined.SystemUpdate,
                    title = "Installed firmware",
                    body = buildString {
                        append(uiState.firmwareVersion.ifBlank { "Version unavailable" })
                        if (uiState.otaAsset.isNotBlank()) append("\nOTA image: ${uiState.otaAsset}")
                    },
                )
                OutlinedTextField(
                    value = ownerDraft,
                    onValueChange = { ownerDraft = it.take(63) },
                    label = { Text("GitHub owner") },
                    supportingText = { Text("You can also use owner/repository.") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = tagDraft,
                    onValueChange = { tagDraft = it.take(63) },
                    label = { Text("Release tag") },
                    supportingText = { Text("Leave blank to follow the latest release.") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Button(
                    onClick = {
                        onUpdateSettings {
                            it.withUpdateOwner(ownerDraft.trim())
                                .withUpdateTag(tagDraft.trim())
                        }
                    },
                    enabled = ownerDraft.isNotBlank() &&
                        (ownerDraft.trim() != settings.updates.owner || tagDraft.trim() != settings.updates.tag),
                ) {
                    Text("Save release source")
                }
                SwitchRow(
                    label = "Check on the Nano",
                    description = "Let the reader check this source automatically when it has internet access.",
                    checked = settings.updates.autoCheck,
                    onCheckedChange = { enabled ->
                        onUpdateSettings { it.withAutomaticUpdateChecks(enabled) }
                    },
                )
            } else {
                SettingsStatusRow(
                    icon = Icons.Outlined.SystemUpdate,
                    title = "Connect to configure releases",
                    body = "The source, current version, and OTA image come directly from your Nano.",
                )
            }
            SwitchRow(
                label = "Update notifications",
                description = "Allow the app to check about once a day and notify once per release.",
                checked = uiState.firmwareNotificationsEnabled,
                onCheckedChange = onFirmwareNotificationsChange,
            )
        }
    }
}

@Composable
private fun ReadingSettings(
    settings: NanoSettings?,
    isConnected: Boolean,
    onUpdateSettings: ((NanoSettings) -> NanoSettings) -> Unit,
) {
    SettingsPage {
        if (settings == null) {
            UnavailableSettings(isConnected)
            return@SettingsPage
        }

        SettingsSection(
            title = "Reading pace",
            subtitle = "Speed and pause behavior while reading.",
        ) {
            SliderRow(
                label = "Base speed",
                description = "Words shown per minute.",
                valueLabel = { value -> "${NanoSettingsSchema.snapWpm(value.toInt())} WPM" },
                value = settings.reading.wpm.toFloat(),
                valueRange = NanoSettingsSchema.WPM_MIN.toFloat()..NanoSettingsSchema.WPM_MAX.toFloat(),
                steps = 0,
                snapValue = { value -> NanoSettingsSchema.snapWpm(value.toInt()).toFloat() },
                onValueChangeFinished = { value -> onUpdateSettings { it.withWpm(value.toInt()) } },
            )
            SegmentedChoiceRow(
                label = "Pause behavior",
                description = "Choose whether pause waits for the sentence to finish.",
                selected = settings.reading.pauseMode,
                options = listOf(
                    NanoSettingsSchema.PAUSE_MODE_SENTENCE_END to "Sentence end",
                    NanoSettingsSchema.PAUSE_MODE_INSTANT to "Immediately",
                ),
                onSelected = { mode -> onUpdateSettings { it.withPauseMode(mode) } },
            )
        }

        SettingsSection(
            title = "Extra delay",
            subtitle = "Add time where text needs more attention.",
        ) {
            PacingSlider(
                label = "Long words",
                value = settings.reading.pacing.longWordMs,
                onChanged = { value -> onUpdateSettings { it.withPacingLongWordMs(value) } },
            )
            PacingSlider(
                label = "Complex words",
                value = settings.reading.pacing.complexWordMs,
                onChanged = { value -> onUpdateSettings { it.withPacingComplexWordMs(value) } },
            )
            PacingSlider(
                label = "Punctuation",
                value = settings.reading.pacing.punctuationMs,
                onChanged = { value -> onUpdateSettings { it.withPacingPunctuationMs(value) } },
            )
            TextButton(
                onClick = {
                    onUpdateSettings {
                        it.withPacingLongWordMs(0)
                            .withPacingComplexWordMs(0)
                            .withPacingPunctuationMs(0)
                    }
                },
            ) {
                Icon(imageVector = Icons.Outlined.Sync, contentDescription = null)
                Text(text = "Reset pacing")
            }
        }
    }
}

@Composable
private fun PacingSlider(
    label: String,
    value: Int,
    onChanged: (Int) -> Unit,
) {
    SliderRow(
        label = label,
        valueLabel = { sliderValue -> "${NanoSettingsSchema.snapPacingMs(sliderValue.toInt())} ms" },
        value = value.toFloat(),
        valueRange = NanoSettingsSchema.PACING_MS_MIN.toFloat()..NanoSettingsSchema.PACING_MS_MAX.toFloat(),
        steps = 11,
        snapValue = { sliderValue -> NanoSettingsSchema.snapPacingMs(sliderValue.toInt()).toFloat() },
        onValueChangeFinished = { sliderValue -> onChanged(sliderValue.toInt()) },
    )
}

@Composable
private fun DisplaySettings(
    uiState: CompanionUiState,
    onUpdateSettings: ((NanoSettings) -> NanoSettings) -> Unit,
    onRefreshThemeCatalog: () -> Unit,
    onSelectCatalogTheme: (String) -> Unit,
    onInstallOnlineTheme: () -> Unit,
    onUploadTheme: () -> Unit,
) {
    SettingsPage {
        val settings = uiState.settings
        if (settings == null) {
            UnavailableSettings(uiState.isConnected)
            return@SettingsPage
        }

        SettingsSection(
            title = "Appearance",
            subtitle = "Theme, brightness, and one-handed layout.",
        ) {
            DropdownRow(
                label = "Theme",
                description = "Colors and typeface settings used by the Nano.",
                selected = settings.display.themeId,
                options = settings.themes.ifEmpty {
                    listOf(NanoTheme(NanoSettingsSchema.THEME_DEFAULT, "Default", builtIn = true))
                }.map { theme -> theme.id to theme.name },
                onSelected = { themeId -> onUpdateSettings { it.withThemeId(themeId) } },
            )
            SliderRow(
                label = "Brightness",
                description = "Applied immediately.",
                valueLabel = { value -> "${(value.toInt() + 1) * 5}%" },
                value = settings.display.brightnessIndex.toFloat(),
                valueRange = NanoSettingsSchema.BRIGHTNESS_MIN.toFloat()..NanoSettingsSchema.BRIGHTNESS_MAX.toFloat(),
                steps = 18,
                onValueChangeFinished = { value -> onUpdateSettings { it.withBrightnessIndex(value.toInt()) } },
            )
            SegmentedChoiceRow(
                label = "Reader hand",
                description = "Moves navigation controls for one-handed use.",
                selected = settings.display.handedness,
                options = listOf(
                    NanoSettingsSchema.HANDEDNESS_LEFT to "Left",
                    NanoSettingsSchema.HANDEDNESS_RIGHT to "Right",
                ),
                onSelected = { hand -> onUpdateSettings { it.withHandedness(hand) } },
            )
        }

        SettingsSection(
            title = "Reader status",
            subtitle = "Choose what the footer and reading screen show.",
        ) {
            DropdownRow(
                label = "Footer label",
                selected = settings.display.footerMetric,
                options = listOf(
                    NanoSettingsSchema.FOOTER_PERCENTAGE to "Book percent",
                    NanoSettingsSchema.FOOTER_CHAPTER_TIME to "Chapter time",
                    NanoSettingsSchema.FOOTER_BOOK_TIME to "Book time",
                ),
                onSelected = { metric -> onUpdateSettings { it.withFooterMetric(metric) } },
            )
            DropdownRow(
                label = "Battery label",
                selected = settings.display.batteryLabel,
                options = listOf(
                    NanoSettingsSchema.BATTERY_PERCENT to "Percent",
                    NanoSettingsSchema.BATTERY_TIME_REMAINING to "Time left",
                    NanoSettingsSchema.BATTERY_VOLTAGE to "Voltage",
                ),
                onSelected = { label -> onUpdateSettings { it.withBatteryLabel(label) } },
            )
            SwitchRow(
                label = "Battery while reading",
                checked = settings.display.readingBattery,
                onCheckedChange = { checked -> onUpdateSettings { it.withReadingBattery(checked) } },
            )
            SwitchRow(
                label = "Chapter while reading",
                checked = settings.display.readingChapter,
                onCheckedChange = { checked -> onUpdateSettings { it.withReadingChapter(checked) } },
            )
            SwitchRow(
                label = "Book progress while reading",
                checked = settings.display.readingProgress,
                onCheckedChange = { checked -> onUpdateSettings { it.withReadingProgress(checked) } },
            )
        }

        SettingsSection(
            title = "Idle screen",
            subtitle = "What happens when the Nano is left alone.",
        ) {
            DropdownRow(
                label = "Screensaver",
                selected = settings.display.screensaver.toString(),
                options = listOf(
                    NanoSettingsSchema.SCREENSAVER_LIFE.toString() to "Life",
                    NanoSettingsSchema.SCREENSAVER_MAZE.toString() to "Maze",
                    NanoSettingsSchema.SCREENSAVER_VORONOI.toString() to "Voronoi",
                    NanoSettingsSchema.SCREENSAVER_REACTION.toString() to "Reaction",
                    NanoSettingsSchema.SCREENSAVER_SCREEN_OFF.toString() to "Screen off",
                ),
                onSelected = { mode ->
                    onUpdateSettings { it.withScreensaver(mode.toIntOrNull() ?: NanoSettingsSchema.SCREENSAVER_LIFE) }
                },
            )
            DropdownRow(
                label = "Standby timer",
                selected = settings.display.standbyTimerIndex.toString(),
                options = listOf(
                    NanoSettingsSchema.STANDBY_TIMER_NEVER.toString() to "Never",
                    NanoSettingsSchema.STANDBY_TIMER_1_MIN.toString() to "1 minute",
                    NanoSettingsSchema.STANDBY_TIMER_5_MIN.toString() to "5 minutes",
                    NanoSettingsSchema.STANDBY_TIMER_15_MIN.toString() to "15 minutes",
                    NanoSettingsSchema.STANDBY_TIMER_30_MIN.toString() to "30 minutes",
                ),
                onSelected = { index ->
                    onUpdateSettings {
                        it.withStandbyTimerIndex(index.toIntOrNull() ?: NanoSettingsSchema.STANDBY_TIMER_NEVER)
                    }
                },
            )
            DropdownRow(
                label = "Language",
                selected = settings.display.language.toString(),
                options = listOf(
                    "0" to "English",
                    "1" to "Español",
                    "2" to "Français",
                    "3" to "Deutsch",
                    "4" to "Română",
                    "5" to "Polski",
                ),
                onSelected = { language -> onUpdateSettings { it.withLanguage(language.toIntOrNull() ?: 0) } },
            )
        }

        SettingsSection(
            title = "Theme library",
            subtitle = "Install from the online catalog or a local theme file.",
            action = {
                IconButton(onClick = onRefreshThemeCatalog) {
                    Icon(imageVector = Icons.Outlined.Sync, contentDescription = "Refresh theme catalog")
                }
            },
        ) {
            Text(text = "From catalog", style = MaterialTheme.typography.labelLarge)
            if (uiState.themeCatalog.isNotEmpty()) {
                DropdownRow(
                    label = "Theme",
                    selected = uiState.selectedCatalogThemeId,
                    options = uiState.themeCatalog.map { theme -> theme.id to theme.name },
                    onSelected = onSelectCatalogTheme,
                )
                Button(
                    onClick = onInstallOnlineTheme,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Icon(imageVector = Icons.Outlined.CloudUpload, contentDescription = null)
                    Text(text = "Install theme")
                }
            } else {
                Text(
                    text = "The online catalog is unavailable. Use refresh to try again.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
            Text(text = "From file", style = MaterialTheme.typography.labelLarge)
            OutlinedButton(onClick = onUploadTheme, modifier = Modifier.fillMaxWidth()) {
                Icon(imageVector = Icons.Outlined.UploadFile, contentDescription = null)
                Text(text = "Choose theme file")
            }
        }
    }
}

@Composable
private fun TypographySettings(
    uiState: CompanionUiState,
    onUpdateSettings: ((NanoSettings) -> NanoSettings) -> Unit,
    onRefreshFontCatalog: () -> Unit,
    onSelectCatalogFont: (String) -> Unit,
    onSelectCatalogFontSize: (String) -> Unit,
    onInstallOnlineFont: () -> Unit,
    onUploadFont: () -> Unit,
) {
    SettingsPage {
        val settings = uiState.settings
        if (settings == null) {
            UnavailableSettings(uiState.isConnected)
            return@SettingsPage
        }

        SettingsSection(
            title = "Text",
            subtitle = "Typeface and spacing for the current theme.",
        ) {
            DropdownRow(
                label = "Typeface",
                description = "Changing this updates only the selected theme.",
                selected = settings.typography.typeface,
                options = settings.fonts.ifEmpty {
                    listOf(NanoFont(NanoSettingsSchema.TYPEFACE_DEFAULT, "Literata", builtIn = true))
                }.map { font -> font.id to font.name },
                onSelected = { typeface -> onUpdateSettings { it.withTypeface(typeface) } },
            )
            SegmentedChoiceRow(
                label = "Font size",
                selected = settings.display.fontSizeIndex.toString(),
                options = listOf("0" to "Large", "1" to "Medium", "2" to "Small"),
                onSelected = { value -> onUpdateSettings { it.withFontSizeIndex(value.toInt()) } },
            )
            SliderRow(
                label = "Tracking",
                description = "Space between letters.",
                valueLabel = { value -> value.toInt().toString() },
                value = settings.typography.tracking.toFloat(),
                valueRange = NanoSettingsSchema.TRACKING_MIN.toFloat()..NanoSettingsSchema.TRACKING_MAX.toFloat(),
                steps = 4,
                onValueChangeFinished = { value -> onUpdateSettings { it.withTracking(value.toInt()) } },
            )
        }

        SettingsSection(
            title = "Reading focus",
            subtitle = "Control the anchor and surrounding context.",
        ) {
            SwitchRow(
                label = "Focus highlight",
                description = "Highlights the current word's focus point.",
                checked = settings.typography.focusHighlight,
                onCheckedChange = { checked -> onUpdateSettings { it.withFocusHighlight(checked) } },
            )
            SwitchRow(
                label = "Phantom words",
                description = "Shows adjacent words as faint context.",
                checked = settings.display.phantomWords,
                onCheckedChange = { checked -> onUpdateSettings { it.withPhantomWords(checked) } },
            )
            SliderRow(
                label = "Anchor position",
                valueLabel = { value -> "${value.toInt()}%" },
                value = settings.typography.anchorPercent.toFloat(),
                valueRange = NanoSettingsSchema.ANCHOR_PERCENT_MIN.toFloat()..NanoSettingsSchema.ANCHOR_PERCENT_MAX.toFloat(),
                steps = 9,
                onValueChangeFinished = { value -> onUpdateSettings { it.withAnchorPercent(value.toInt()) } },
            )
            SliderRow(
                label = "Guide width",
                valueLabel = { value -> NanoSettingsSchema.snapGuideWidth(value.toInt()).toString() },
                value = settings.typography.guideWidth.toFloat(),
                valueRange = NanoSettingsSchema.GUIDE_WIDTH_MIN.toFloat()..NanoSettingsSchema.GUIDE_WIDTH_MAX.toFloat(),
                steps = 8,
                snapValue = { value -> NanoSettingsSchema.snapGuideWidth(value.toInt()).toFloat() },
                onValueChangeFinished = { value -> onUpdateSettings { it.withGuideWidth(value.toInt()) } },
            )
            SliderRow(
                label = "Guide gap",
                valueLabel = { value -> value.toInt().toString() },
                value = settings.typography.guideGap.toFloat(),
                valueRange = NanoSettingsSchema.GUIDE_GAP_MIN.toFloat()..NanoSettingsSchema.GUIDE_GAP_MAX.toFloat(),
                steps = 5,
                onValueChangeFinished = { value -> onUpdateSettings { it.withGuideGap(value.toInt()) } },
            )
        }

        SettingsSection(
            title = "Font library",
            subtitle = "Install from the online catalog or a local .rfont4 file.",
            action = {
                IconButton(onClick = onRefreshFontCatalog) {
                    Icon(imageVector = Icons.Outlined.Sync, contentDescription = "Refresh font catalog")
                }
            },
        ) {
            Text(text = "From catalog", style = MaterialTheme.typography.labelLarge)
            if (uiState.fontCatalog.isNotEmpty()) {
                val selectedFont = uiState.fontCatalog.firstOrNull { it.id == uiState.selectedCatalogFontId }
                    ?: uiState.fontCatalog.firstOrNull()
                DropdownRow(
                    label = "Font",
                    selected = uiState.selectedCatalogFontId,
                    options = uiState.fontCatalog.map { font -> font.id to font.name },
                    onSelected = onSelectCatalogFont,
                )
                SegmentedChoiceRow(
                    label = "Font size to install",
                    selected = uiState.selectedCatalogFontSize,
                    options = listOf("large" to "Large", "medium" to "Medium", "small" to "Small")
                        .filter { (size, _) -> selectedFont?.files?.containsKey(size) ?: true },
                    onSelected = onSelectCatalogFontSize,
                )
                Button(
                    onClick = onInstallOnlineFont,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Icon(imageVector = Icons.Outlined.CloudUpload, contentDescription = null)
                    Text(text = "Install font")
                }
            } else {
                Text(
                    text = "The online catalog is unavailable. Use refresh to try again.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
            Text(text = "From file", style = MaterialTheme.typography.labelLarge)
            OutlinedButton(onClick = onUploadFont, modifier = Modifier.fillMaxWidth()) {
                Icon(imageVector = Icons.Outlined.UploadFile, contentDescription = null)
                Text(text = "Choose .rfont4 file")
            }
        }
    }
}

@Composable
private fun UnavailableSettings(isConnected: Boolean) {
    Text(
        text = if (isConnected) "Settings are not loaded yet." else "Connect to the Nano to edit reader settings.",
        modifier = Modifier.padding(vertical = 24.dp),
        style = MaterialTheme.typography.bodyLarge,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}
