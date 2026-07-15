package com.rsvpnano.android.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.rsvpnano.app.NanoWifiConnector
import com.rsvpnano.app.createAndroidCompanionPresenter
import java.io.File

class CompanionViewModel(
    appFilesDir: File,
    nanoNetworkController: NanoWifiConnector,
) : ViewModel() {
    val presenter = createAndroidCompanionPresenter(
        appFilesDir = appFilesDir,
        nanoWifiConnector = nanoNetworkController,
        scope = viewModelScope,
    )

    override fun onCleared() {
        presenter.close()
        super.onCleared()
    }

    class Factory(
        private val appFilesDir: File,
        private val nanoNetworkController: NanoWifiConnector,
    ) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            return CompanionViewModel(
                appFilesDir = appFilesDir,
                nanoNetworkController = nanoNetworkController,
            ) as T
        }
    }
}
