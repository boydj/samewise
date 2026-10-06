import SamewiseKit
import SwiftUI

@main
struct SamewiseApp: App {
    @State private var session: RadioSession

    init() {
        #if targetEnvironment(simulator)
        let simulator = true
        #else
        let simulator = false
        #endif
        let options = LaunchOptions(arguments: ProcessInfo.processInfo.arguments, simulator: simulator)
        _session = State(initialValue: RadioSession(link: options.makeLink()))
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environment(session)
        }
    }
}
