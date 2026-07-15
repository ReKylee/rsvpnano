package com.rsvpnano.api

class NanoClientError(
    message: String,
    val code: String? = null,
    val field: String? = null,
    val status: Int? = null,
    cause: Throwable? = null,
) : IllegalStateException(message, cause)
