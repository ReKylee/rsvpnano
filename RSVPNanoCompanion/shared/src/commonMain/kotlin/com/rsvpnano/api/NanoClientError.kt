package com.rsvpnano.api

class NanoClientError(
    message: String,
    val status: Int? = null,
    cause: Throwable? = null,
) : IllegalStateException(message, cause)
