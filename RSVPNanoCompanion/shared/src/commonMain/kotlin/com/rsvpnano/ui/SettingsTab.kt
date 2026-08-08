package com.rsvpnano.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.outlined.MenuBook
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.CloudUpload
import androidx.compose.material.icons.outlined.Language
import androidx.compose.material.icons.outlined.Info
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
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.rsvpnano.models.NanoLocales
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoSettingsSchema
import com.rsvpnano.app.releaseSource

private enum class SettingsCategory(
    val label: String,
    val icon: ImageVector,
    val help: String,
) {
    Device("Reader & network", Icons.Outlined.Wifi, "Connect to your reader, configure its Wi-Fi, and choose its update source."),
    Reading("Reading", Icons.AutoMirrored.Outlined.MenuBook, "Set reading speed, pacing, pauses, footer information, and controls."),
    Typography("Typography", Icons.Outlined.TextFields, "Adjust reader text size, tracking, focus highlight, and guide placement."),
    Display("Display", Icons.Outlined.Palette, "Choose the reader theme, brightness, standby behavior, and screensaver."),
    Locales("Languages", Icons.Outlined.Language, "Choose the interface language or install a locale pack. Reader language support comes from fonts."),
    Fonts("Fonts", Icons.Outlined.CloudUpload, "Choose the default reading typeface and install fonts for the scripts used by your books."),
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
    onUploadLocalePack: () -> Unit,
    onHelpChanged: (String, String) -> Unit,
) {
    var categoryName by rememberSaveable { mutableStateOf(SettingsCategory.Device.name) }
    var showMobileCategory by rememberSaveable { mutableStateOf(false) }
    var showAbout by rememberSaveable { mutableStateOf(false) }
    val category = SettingsCategory.valueOf(categoryName)
    val content: @Composable (Modifier) -> Unit = { modifier ->
        if (showAbout) {
            AboutPage(modifier)
        } else {
            SettingsContent(
                category = category,
                uiState = uiState,
                presenter = presenter,
                onFirmwareNotificationsChange = onFirmwareNotificationsChange,
                hasPermissions = hasPermissions,
                onGrantPermissions = onGrantPermissions,
                onUploadTheme = onUploadTheme,
                onUploadFont = onUploadFont,
                onUploadLocalePack = onUploadLocalePack,
                modifier = modifier,
            )
        }
    }

    PullRefreshBox(
        isRefreshing = uiState.isRefreshing,
        onRefresh = presenter::refresh,
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
            val help = when {
                showAbout -> "About" to "Project links and creator credit for the RSVP Nano companion."
                maxWidth >= 720.dp || showMobileCategory -> category.label to category.help
                else -> "Settings" to "Choose a section to configure your reader, its display, languages, or fonts."
            }
            SideEffect { onHelpChanged(help.first, help.second) }
            if (maxWidth >= 720.dp) {
                Row(modifier = Modifier.fillMaxSize()) {
                    SettingsRail(
                        selected = category.takeUnless { showAbout },
                        aboutSelected = showAbout,
                        onSelected = {
                            categoryName = it.name
                            showAbout = false
                        },
                        onAbout = { showAbout = true },
                    )
                    VerticalDivider()
                    content(Modifier.weight(1f))
                }
            } else {
                if (!showMobileCategory && !showAbout) {
                    SettingsIndex(
                        uiState = uiState,
                        onSelected = {
                            categoryName = it.name
                            showMobileCategory = true
                        },
                        onAbout = { showAbout = true },
                    )
                } else {
                    Column(modifier = Modifier.fillMaxSize()) {
                        TextButton(
                            onClick = {
                                showMobileCategory = false
                                showAbout = false
                            },
                        ) {
                            Icon(Icons.AutoMirrored.Outlined.ArrowBack, contentDescription = null)
                            Text("Settings")
                        }
                        content(Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun SettingsRail(
    selected: SettingsCategory?,
    aboutSelected: Boolean,
    onSelected: (SettingsCategory) -> Unit,
    onAbout: () -> Unit,
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
        HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))
        NavigationRailItem(
            selected = aboutSelected,
            onClick = onAbout,
            icon = { Icon(Icons.Outlined.Info, contentDescription = null) },
            label = { Text("About") },
        )
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
    onUploadLocalePack: () -> Unit,
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
            )

            SettingsCategory.Locales -> LocaleSettings(
                uiState = uiState,
                onUpdateSettings = presenter::updateSettings,
                onUploadLocalePack = onUploadLocalePack,
                onRemoveLocalePack = presenter::removeLocalePack,
                onRefreshLocaleCatalog = presenter::refreshLocaleCatalog,
                onInstallOnlineLocale = presenter::installOnlineLocalePack,
            )

            SettingsCategory.Fonts -> FontSettings(
                uiState = uiState,
                onUpdateSettings = presenter::updateSettings,
                onRefreshFontCatalog = presenter::refreshFontCatalog,
                onInstallOnlineFont = presenter::installOnlineFont,
                onUploadFont = onUploadFont,
                onRemoveFont = presenter::removeFont,
            )
        }
    }
}

@Composable
private fun SettingsPage(
    content: @Composable () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .widthIn(max = 760.dp)
            .verticalScroll(rememberScrollState())
            .padding(start = 20.dp, top = 8.dp, end = 20.dp, bottom = 24.dp),
    ) {
        content()
    }
}

@Composable
private fun AboutPage(modifier: Modifier = Modifier) {
    val uriHandler = LocalUriHandler.current
    Column(
        modifier = modifier
            .fillMaxSize()
            .widthIn(max = 560.dp)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 24.dp, vertical = 48.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Surface(
            color = MaterialTheme.colorScheme.primaryContainer,
            shape = MaterialTheme.shapes.extraLarge,
        ) {
            Icon(
                Icons.AutoMirrored.Outlined.MenuBook,
                contentDescription = null,
                modifier = Modifier.padding(24.dp),
                tint = MaterialTheme.colorScheme.onPrimaryContainer,
            )
        }
        Text("RSVP Nano", style = MaterialTheme.typography.headlineMedium)
        Text(
            "A quiet companion for adding reading material and configuring your reader.",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text("Created by ReKylee", style = MaterialTheme.typography.titleSmall)
        FilledTonalButton(onClick = { uriHandler.openUri("https://github.com/ReKylee/rsvpnano") }) {
            Icon(GitHubIcon, contentDescription = null)
            Text("View on GitHub")
        }
    }
}

private val GitHubIcon by lazy {
    ImageVector.Builder(
        name = "GitHub",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f,
    ).apply {
        addPath(
            pathData = PathParser().parsePathString(
                "M10.226 17.284c-2.965-.36-5.054-2.493-5.054-5.256 0-1.123.404-2.336 1.078-3.144-.292-.741-.247-2.314.09-2.965.898-.112 2.111.36 2.83 1.01.853-.269 1.752-.404 2.853-.404 1.1 0 1.999.135 2.807.382.696-.629 1.932-1.1 2.83-.988.315.606.36 2.179.067 2.942.72.854 1.101 2 1.101 3.167 0 2.763-2.089 4.852-5.098 5.234.763.494 1.28 1.572 1.28 2.807v2.336c0 .674.561 1.056 1.235.786 4.066-1.55 7.255-5.615 7.255-10.646C23.5 6.188 18.334 1 11.978 1 5.62 1 .5 6.188.5 12.545c0 4.986 3.167 9.12 7.435 10.669.606.225 1.19-.18 1.19-.786V20.63a2.9 2.9 0 0 1-1.078.224c-1.483 0-2.359-.808-2.987-2.313-.247-.607-.517-.966-1.034-1.033-.27-.023-.359-.135-.359-.27 0-.27.45-.471.898-.471.652 0 1.213.404 1.797 1.235.45.651.921.943 1.483.943.561 0 .92-.202 1.437-.719.382-.381.674-.718.944-.943",
            ).toNodes(),
            fill = SolidColor(Color.Black),
        )
    }.build()
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
                val wifiStatus = uiState.settings.network.wifiSsid
                    .takeIf(String::isNotBlank)
                    ?.let { "Saved network: $it" }
                    ?: "No saved network"
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
                var ownerDraft by remember(settings.updates.repositoryOwner) {
                    mutableStateOf(settings.updates.repositoryOwner)
                }
                var tagDraft by remember(settings.updates.releaseTag) {
                    mutableStateOf(settings.updates.releaseTag)
                }
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
                        (ownerDraft.trim() != settings.updates.repositoryOwner ||
                            tagDraft.trim() != settings.updates.releaseTag),
                ) {
                    Text("Save release source")
                }
                SwitchRow(
                    label = "Check on the Nano",
                    description = "Let the reader check this source automatically when it has internet access.",
                    checked = settings.updates.automatic,
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
                value = settings.reading.pacing.longWordDelayMs,
                onChanged = { value -> onUpdateSettings { it.withPacingLongWordMs(value) } },
            )
            PacingSlider(
                label = "Complex words",
                value = settings.reading.pacing.complexWordDelayMs,
                onChanged = { value -> onUpdateSettings { it.withPacingComplexWordMs(value) } },
            )
            PacingSlider(
                label = "Punctuation",
                value = settings.reading.pacing.punctuationDelayMs,
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
                selected = settings.`interface`.selectedThemeId,
                options = uiState.availableThemes
                    .map { theme -> theme.id to theme.name }
                    .ifEmpty { listOf(settings.`interface`.selectedThemeId to settings.`interface`.selectedThemeId) },
                onSelected = { themeId -> onUpdateSettings { it.withThemeId(themeId) } },
            )
            SliderRow(
                label = "Brightness",
                description = "Applied immediately.",
                valueLabel = { value -> "${value.toInt()}%" },
                value = settings.`interface`.brightnessPercent.toFloat(),
                valueRange = NanoSettingsSchema.BRIGHTNESS_MIN.toFloat()..NanoSettingsSchema.BRIGHTNESS_MAX.toFloat(),
                steps = 18,
                onValueChangeFinished = { value -> onUpdateSettings { it.withBrightnessPercent(value.toInt()) } },
            )
            SegmentedChoiceRow(
                label = "Reader hand",
                description = "Moves navigation controls for one-handed use.",
                selected = if (settings.reading.leftHanded) {
                    NanoSettingsSchema.HANDEDNESS_LEFT
                } else {
                    NanoSettingsSchema.HANDEDNESS_RIGHT
                },
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
                selected = settings.reading.footerMetric,
                options = listOf(
                    NanoSettingsSchema.FOOTER_PERCENTAGE to "Book percent",
                    NanoSettingsSchema.FOOTER_CHAPTER_TIME to "Chapter time",
                    NanoSettingsSchema.FOOTER_BOOK_TIME to "Book time",
                ),
                onSelected = { metric -> onUpdateSettings { it.withFooterMetric(metric) } },
            )
            DropdownRow(
                label = "Battery label",
                selected = settings.reading.batteryLabel,
                options = listOf(
                    NanoSettingsSchema.BATTERY_PERCENTAGE to "Percent",
                    NanoSettingsSchema.BATTERY_TIME_REMAINING to "Time left",
                    NanoSettingsSchema.BATTERY_VOLTAGE to "Voltage",
                ),
                onSelected = { label -> onUpdateSettings { it.withBatteryLabel(label) } },
            )
            SwitchRow(
                label = "Battery icon",
                checked = settings.reading.batteryIconVisible,
                onCheckedChange = { checked -> onUpdateSettings { it.withBatteryIconVisible(checked) } },
            )
            SwitchRow(
                label = "Battery while reading",
                checked = settings.reading.batteryVisibleWhileReading,
                onCheckedChange = { checked -> onUpdateSettings { it.withReadingBattery(checked) } },
            )
            SwitchRow(
                label = "Chapter while reading",
                checked = settings.reading.chapterVisibleWhileReading,
                onCheckedChange = { checked -> onUpdateSettings { it.withReadingChapter(checked) } },
            )
            SwitchRow(
                label = "Book progress while reading",
                checked = settings.reading.progressVisibleWhileReading,
                onCheckedChange = { checked -> onUpdateSettings { it.withReadingProgress(checked) } },
            )
        }

        SettingsSection(
            title = "Idle screen",
            subtitle = "What happens when the Nano is left alone.",
        ) {
            DropdownRow(
                label = "Screensaver",
                selected = settings.`interface`.screensaver,
                options = listOf(
                    NanoSettingsSchema.SCREENSAVER_LIFE to "Life",
                    NanoSettingsSchema.SCREENSAVER_MAZE to "Maze",
                    NanoSettingsSchema.SCREENSAVER_VORONOI to "Voronoi",
                    NanoSettingsSchema.SCREENSAVER_REACTION to "Reaction",
                    NanoSettingsSchema.SCREENSAVER_SCREEN_OFF to "Screen off",
                ),
                onSelected = { mode ->
                    onUpdateSettings { it.withScreensaver(mode) }
                },
            )
            DropdownRow(
                label = "Standby timer",
                selected = settings.`interface`.standbyTimerIndex.toString(),
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
) {
    SettingsPage {
        val settings = uiState.settings
        if (settings == null) {
            UnavailableSettings(uiState.isConnected)
            return@SettingsPage
        }

        SettingsSection(
            title = "Text",
            subtitle = "Reader text size and spacing.",
        ) {
            SegmentedChoiceRow(
                label = "Font size",
                selected = settings.reading.typography.fontSizeIndex.toString(),
                options = listOf("0" to "Large", "1" to "Medium", "2" to "Small"),
                onSelected = { value -> onUpdateSettings { it.withFontSizeIndex(value.toInt()) } },
            )
            SliderRow(
                label = "Tracking",
                description = "Space between letters.",
                valueLabel = { value -> value.toInt().toString() },
                value = settings.reading.typography.tracking.toFloat(),
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
                checked = settings.reading.typography.focusHighlight,
                onCheckedChange = { checked -> onUpdateSettings { it.withFocusHighlight(checked) } },
            )
            SwitchRow(
                label = "Phantom words",
                description = "Shows adjacent words as faint context.",
                checked = settings.reading.phantomWords,
                onCheckedChange = { checked -> onUpdateSettings { it.withPhantomWords(checked) } },
            )
            SliderRow(
                label = "Anchor position",
                valueLabel = { value -> "${value.toInt()}%" },
                value = settings.reading.typography.anchor.toFloat(),
                valueRange = NanoSettingsSchema.ANCHOR_PERCENT_MIN.toFloat()..NanoSettingsSchema.ANCHOR_PERCENT_MAX.toFloat(),
                steps = 9,
                onValueChangeFinished = { value -> onUpdateSettings { it.withAnchorPercent(value.toInt()) } },
            )
            SliderRow(
                label = "Guide width",
                valueLabel = { value -> NanoSettingsSchema.snapGuideWidth(value.toInt()).toString() },
                value = settings.reading.typography.guideWidth.toFloat(),
                valueRange = NanoSettingsSchema.GUIDE_WIDTH_MIN.toFloat()..NanoSettingsSchema.GUIDE_WIDTH_MAX.toFloat(),
                steps = 8,
                snapValue = { value -> NanoSettingsSchema.snapGuideWidth(value.toInt()).toFloat() },
                onValueChangeFinished = { value -> onUpdateSettings { it.withGuideWidth(value.toInt()) } },
            )
            SliderRow(
                label = "Guide gap",
                valueLabel = { value -> value.toInt().toString() },
                value = settings.reading.typography.guideGap.toFloat(),
                valueRange = NanoSettingsSchema.GUIDE_GAP_MIN.toFloat()..NanoSettingsSchema.GUIDE_GAP_MAX.toFloat(),
                steps = 5,
                onValueChangeFinished = { value -> onUpdateSettings { it.withGuideGap(value.toInt()) } },
            )
        }

    }
}

@Composable
private fun LocaleSettings(
    uiState: CompanionUiState,
    onUpdateSettings: ((NanoSettings) -> NanoSettings) -> Unit,
    onUploadLocalePack: () -> Unit,
    onRemoveLocalePack: (String) -> Unit,
    onRefreshLocaleCatalog: () -> Unit,
    onInstallOnlineLocale: (String) -> Unit,
) {
    SettingsPage {
        val settings = uiState.settings
        if (settings == null) {
            UnavailableSettings(uiState.isConnected)
            return@SettingsPage
        }
        SettingsSection(
            title = "Interface locale",
            subtitle = "Reader language support comes from installed fonts.",
        ) {
            DropdownRow(
                label = "Interface language",
                selected = settings.`interface`.locale,
                options = buildList {
                    add(NanoLocales.DEFAULT to "English")
                    uiState.availableLocales.filter { it.locale.isNotBlank() }
                        .forEach { add(it.locale to it.nativeName) }
                }.distinctBy { it.first },
                onSelected = { locale -> onUpdateSettings { it.withLocale(locale) } },
            )
        }
        SettingsSection(
            title = "Installed",
            subtitle = "These affect only interface text and its compact UI font.",
        ) {
            if (uiState.availableLocales.isEmpty()) {
                Text("No external locale packs installed.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            uiState.availableLocales.forEach { localePack ->
                AssetRow(
                    title = localePack.nativeName,
                    subtitle = localeDetails(
                        localePack.englishName,
                        localePack.direction,
                        localePack.translationStatus,
                    ),
                    action = "Remove",
                    onAction = { onRemoveLocalePack(localePack.id) },
                )
            }
        }
        val installedIds = uiState.availableLocales.mapTo(mutableSetOf()) { it.id }
        SettingsSection(
            title = "Available from ${catalogSource(settings)}",
            action = {
                IconButton(onClick = onRefreshLocaleCatalog) {
                    Icon(Icons.Outlined.Sync, contentDescription = "Refresh locale catalog")
                }
            },
        ) {
            val available = uiState.localeCatalog.filterNot { it.id in installedIds }
            if (available.isEmpty()) {
                Text(
                    if (uiState.localeCatalog.isEmpty()) "The online locale catalog is unavailable." else "All available locale packs are installed.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            available.forEach { pack ->
                AssetRow(
                    title = pack.name,
                    subtitle = localeDetails(pack.englishName, pack.direction, pack.translationStatus, pack.version),
                    action = "Install",
                    onAction = { onInstallOnlineLocale(pack.id) },
                )
            }
            UploadRow("Install locale pack from ZIP", onUploadLocalePack)
        }
    }
}

@Composable
private fun SettingsIndex(
    uiState: CompanionUiState,
    onSelected: (SettingsCategory) -> Unit,
    onAbout: () -> Unit,
) {
    val settings = uiState.settings
    val summaries = mapOf(
        SettingsCategory.Device to if (uiState.isConnected) {
            "Connected to ${uiState.currentNano?.ssid ?: "Nano"} · ${settings?.network?.wifiSsid?.takeIf(String::isNotBlank) ?: "No internet Wi-Fi"}"
        } else {
            "Not connected"
        },
        SettingsCategory.Reading to settings?.let { "${it.reading.wpm} WPM · ${it.reading.pauseMode.replace('-', ' ')} pause" }.orEmpty(),
        SettingsCategory.Typography to settings?.let {
            val font = uiState.availableFonts.firstOrNull { font -> font.id == it.reading.typography.fontId }?.name
                ?: it.reading.typography.fontId
            val size = listOf("Large", "Medium", "Small").getOrElse(it.reading.typography.fontSizeIndex) { "Default" }
            "$font · $size · Tracking ${it.reading.typography.tracking}"
        }.orEmpty(),
        SettingsCategory.Display to settings?.let {
            val theme = uiState.availableThemes.firstOrNull { theme -> theme.id == it.`interface`.selectedThemeId }?.name
                ?: it.`interface`.selectedThemeId
            "$theme · ${it.`interface`.brightnessPercent}% brightness"
        }.orEmpty(),
        SettingsCategory.Locales to settings?.let {
            val locale = uiState.availableLocales.firstOrNull { pack -> pack.locale == it.`interface`.locale }?.nativeName
                ?: "English"
            "$locale · ${uiState.availableLocales.size} locale packs installed"
        }.orEmpty(),
        SettingsCategory.Fonts to settings?.let {
            val font = uiState.availableFonts.firstOrNull { installed -> installed.id == it.reading.typography.fontId }?.name
                ?: it.reading.typography.fontId
            "$font · ${uiState.availableFonts.count { installed -> !installed.builtIn }} reader fonts installed"
        }.orEmpty(),
    )
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
    ) {
        SettingsCategory.entries.forEach { option ->
            ListItem(
                headlineContent = { Text(option.label) },
                supportingContent = summaries[option]?.takeIf(String::isNotBlank)?.let { summary ->
                    { Text(summary, maxLines = 1, overflow = TextOverflow.Ellipsis) }
                },
                leadingContent = { Icon(option.icon, contentDescription = null) },
                trailingContent = { Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, contentDescription = null) },
                modifier = Modifier.clickable { onSelected(option) },
            )
            HorizontalDivider(modifier = Modifier.padding(start = 56.dp))
        }
        ListItem(
            headlineContent = { Text("About") },
            supportingContent = { Text("RSVP Nano companion") },
            leadingContent = { Icon(Icons.Outlined.Info, contentDescription = null) },
            trailingContent = { Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, contentDescription = null) },
            modifier = Modifier.clickable(onClick = onAbout),
        )
    }
}

@Composable
private fun FontSettings(
    uiState: CompanionUiState,
    onUpdateSettings: ((NanoSettings) -> NanoSettings) -> Unit,
    onRefreshFontCatalog: () -> Unit,
    onInstallOnlineFont: (String) -> Unit,
    onUploadFont: () -> Unit,
    onRemoveFont: (String) -> Unit,
) {
    SettingsPage {
        val settings = uiState.settings
        if (settings == null) {
            UnavailableSettings(uiState.isConnected)
            return@SettingsPage
        }
        SettingsSection(
            title = "Default reader font",
            subtitle = "Used for every book unless that book selects a compatible font for a language.",
        ) {
            DropdownRow(
                label = "Typeface",
                selected = settings.reading.typography.fontId,
                options = uiState.availableFonts.map { it.id to it.name }
                    .ifEmpty { listOf(settings.reading.typography.fontId to settings.reading.typography.fontId) },
                onSelected = { typeface -> onUpdateSettings { it.withTypeface(typeface) } },
            )
        }
        SettingsSection(
            title = "Installed",
            subtitle = "Only compatible fonts appear when choosing a typeface for a book language.",
        ) {
            uiState.availableFonts.forEach { font ->
                AssetRow(
                    title = font.name,
                    subtitle = fontDetails(font.scriptMask, font.builtIn, font.shaping),
                    action = if (font.builtIn) null else "Remove",
                    onAction = { onRemoveFont(font.id) },
                )
            }
        }
        val installedIds = uiState.availableFonts.mapTo(mutableSetOf()) { it.id }
        SettingsSection(
            title = "Available from ${catalogSource(settings)}",
            action = {
                IconButton(onClick = onRefreshFontCatalog) {
                    Icon(imageVector = Icons.Outlined.Sync, contentDescription = "Refresh font catalog")
                }
            },
        ) {
            val available = uiState.fontCatalog.filterNot { it.id in installedIds }
            if (available.isEmpty()) {
                Text(
                    if (uiState.fontCatalog.isEmpty()) "The online font catalog is unavailable." else "All available fonts are installed.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            available.forEach { font ->
                AssetRow(
                    title = font.name,
                    subtitle = fontDetails(font.scriptMask, builtIn = false, shaping = font.shaping),
                    action = "Install",
                    onAction = { onInstallOnlineFont(font.id) },
                )
            }
            UploadRow("Install local .rfont4 file", onUploadFont)
        }
    }
}

@Composable
private fun AssetRow(
    title: String,
    subtitle: String,
    action: String?,
    onAction: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(
                subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (action != null) TextButton(onClick = onAction) { Text(action) }
    }
    HorizontalDivider(modifier = Modifier.padding(start = 12.dp))
}

@Composable
private fun UploadRow(label: String, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(label) },
        leadingContent = { Icon(Icons.Outlined.UploadFile, contentDescription = null) },
        trailingContent = { Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, contentDescription = null) },
        modifier = Modifier.clickable(onClick = onClick),
    )
}

private fun catalogSource(settings: NanoSettings): String {
    val source = releaseSource(settings.updates.repositoryOwner, settings.updates.releaseTag)
        ?: return "configured repository"
    return "${source.owner}/${source.repository}@${source.tag.ifBlank { "main" }}"
}

private fun localeDetails(
    englishName: String,
    direction: String,
    status: String,
    version: String = "",
): String = listOf(
    englishName.replace("ChineseSimplified", "Simplified Chinese").replace("ChineseTraditional", "Traditional Chinese"),
    if (direction.equals("rtl", ignoreCase = true)) "Right-to-left" else "Left-to-right",
    status.replaceFirstChar(Char::uppercase),
    version.takeIf(String::isNotBlank)?.let { "v$it" }.orEmpty(),
).filter(String::isNotBlank).joinToString(" · ")

private val ScriptNames = listOf(
    1 shl 0 to "Latin",
    1 shl 1 to "Cyrillic",
    1 shl 2 to "Greek",
    1 shl 3 to "Hebrew",
    1 shl 4 to "Arabic",
    1 shl 5 to "Han",
    1 shl 6 to "Hiragana",
    1 shl 7 to "Katakana",
    1 shl 8 to "Hangul",
    1 shl 9 to "Math",
)

internal fun fontDetails(scriptMask: Int, builtIn: Boolean, shaping: Boolean): String =
    (ScriptNames.filter { (mask) -> scriptMask and mask != 0 }.map { it.second } +
        listOfNotNull("Built in".takeIf { builtIn }, "Shaping".takeIf { shaping }))
        .joinToString(" · ")
        .ifBlank { "Reader font" }

@Composable
private fun UnavailableSettings(isConnected: Boolean) {
    Text(
        text = if (isConnected) "Settings are not loaded yet." else "Connect to the Nano to edit reader settings.",
        modifier = Modifier.padding(vertical = 24.dp),
        style = MaterialTheme.typography.bodyLarge,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}
