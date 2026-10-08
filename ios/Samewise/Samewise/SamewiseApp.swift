import SamewiseKit
import SwiftUI

/// What the views need besides the session.
@MainActor
@Observable
final class AppServices {
    let location: LocationLookup

    init(location: LocationLookup) {
        self.location = location
    }
}

@main
struct SamewiseApp: App {
    @State private var session: RadioSession
    @State private var services: AppServices

    init() {
        #if targetEnvironment(simulator)
        let simulator = true
        #else
        let simulator = false
        #endif
        let options = LaunchOptions(arguments: ProcessInfo.processInfo.arguments, simulator: simulator)
        let session = RadioSession(link: options.makeLink())
        let services = AppServices(location: options.makeLocationLookup())
        session.onConnect = [session.travelRefreshStep(services.location)]
        _session = State(initialValue: session)
        _services = State(initialValue: services)
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environment(session)
                .environment(services)
        }
    }
}
