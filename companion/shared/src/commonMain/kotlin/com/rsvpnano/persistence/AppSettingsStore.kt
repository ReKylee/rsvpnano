package com.rsvpnano.persistence

import com.rsvpnano.models.CompanionAppSettings
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json

/** Shared companion preferences and cached device state. */
interface AppSettingsStore {
    suspend fun load(): CompanionAppSettings
    suspend fun save(settings: CompanionAppSettings)
}

class JsonAppSettingsStore(
    private val storage: TextStorage,
    private val json: Json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
        explicitNulls = false
        prettyPrint = false
    },
) : AppSettingsStore {
    override suspend fun load(): CompanionAppSettings {
        val text = storage.readText()?.takeIf { it.isNotBlank() } ?: return CompanionAppSettings()
        return runCatching {
            json.decodeFromString(CompanionAppSettings.serializer(), text)
        }.recoverCatching { error ->
            if (error is SerializationException || error is IllegalArgumentException) {
                CompanionAppSettings()
            } else {
                throw error
            }
        }.getOrThrow()
    }

    override suspend fun save(settings: CompanionAppSettings) {
        storage.writeText(json.encodeToString(CompanionAppSettings.serializer(), settings))
    }
}
