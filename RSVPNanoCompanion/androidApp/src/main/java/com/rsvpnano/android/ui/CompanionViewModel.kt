package com.rsvpnano.android.ui

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.rsvpnano.android.net.AndroidNanoNetworkController
import com.rsvpnano.app.createAndroidCompanionPresenter
import java.io.File

class CompanionViewModel(
    appFilesDir: File,
    val nanoNetworkController: AndroidNanoNetworkController,
) : ViewModel() {
    val presenter = createAndroidCompanionPresenter(
        appFilesDir = appFilesDir,
        nanoWifiConnector = nanoNetworkController,
        nanoSocketFactory = nanoNetworkController.socketFactory,
        scope = viewModelScope,
    )

    override fun onCleared() {
        presenter.close()
        super.onCleared()
    }

    class Factory(
        context: Context,
    ) : ViewModelProvider.Factory {
        private val appContext = context.applicationContext

        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            return CompanionViewModel(
                appFilesDir = appContext.filesDir,
                nanoNetworkController = AndroidNanoNetworkController(appContext),
            ) as T
        }
    }
}
