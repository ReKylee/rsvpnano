package com.rsvpnano.persistence

interface TextStorage {
    suspend fun readText(): String?
    suspend fun writeText(value: String)
}
