package com.rsvpnano.api

class NanoClientError(
    message: String,
    val status: Int? = null,
    val code: String? = null,
    val field: String? = null,
    cause: Throwable? = null,
) : IllegalStateException(message, cause)
