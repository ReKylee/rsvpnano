import SwiftUI
import shared
import BackgroundTasks
import UserNotifications

@main
struct RSVPNanoCompanionApp: App {
    private let firmwareUpdates = IosSharedWiringKt.iosFirmwareUpdates()

    init() {
        FirmwareUpdateNotifications.register(firmwareUpdates)
    }

    var body: some Scene {
        WindowGroup {
            ComposeCompanionRoot()
                .ignoresSafeArea(.keyboard)
                .onAppear {
                    FirmwareUpdateNotifications.check(firmwareUpdates)
                }
        }
    }
}

private enum FirmwareUpdateNotifications {
    static let taskIdentifier = "com.rsvpnano.companion.firmware-update"

    static func register(_ updates: FirmwareUpdates) {
        BGTaskScheduler.shared.register(forTaskWithIdentifier: taskIdentifier, using: nil) { task in
            guard let refreshTask = task as? BGAppRefreshTask else { return }
            var finished = false
            let finish: (Bool) -> Void = { success in
                guard !finished else { return }
                finished = true
                refreshTask.setTaskCompleted(success: success)
            }
            refreshTask.expirationHandler = { finish(false) }
            schedule()
            check(updates, completion: finish)
        }
        schedule()
    }

    static func schedule() {
        let request = BGAppRefreshTaskRequest(identifier: taskIdentifier)
        request.earliestBeginDate = Date(timeIntervalSinceNow: 24 * 60 * 60)
        try? BGTaskScheduler.shared.submit(request)
    }

    static func check(_ updates: FirmwareUpdates, completion: @escaping (Bool) -> Void = { _ in }) {
        updates.pendingNotification { update, error in
            guard error == nil, let update else {
                completion(error == nil)
                return
            }

            let content = UNMutableNotificationContent()
            content.title = "RSVP Nano update available"
            content.body = "Firmware \(update.availableVersion) is ready for your Nano."
            content.sound = .default
            let request = UNNotificationRequest(
                identifier: "firmware-\(update.availableVersion)",
                content: content,
                trigger: nil
            )
            UNUserNotificationCenter.current().add(request) { notificationError in
                guard notificationError == nil else {
                    completion(false)
                    return
                }
                updates.markNotified(version: update.availableVersion) { markError in
                    completion(markError == nil)
                }
            }
        }
    }
}

private struct ComposeCompanionRoot: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> UIViewController {
        RsvpNanoComposeKt.RsvpNanoComposeViewController()
    }

    func updateUIViewController(_ uiViewController: UIViewController, context: Context) {
    }
}
