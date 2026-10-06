import SamewiseKit
import SwiftUI

/// Connect first; once the radio's settings are read, the main tabs.
struct RootView: View {
    @Environment(RadioSession.self) private var session

    var body: some View {
        Group {
            if session.linkState == .connected && session.loaded {
                MainTabs()
            } else {
                ConnectView()
            }
        }
        .overlay(alignment: .bottom) {
            if session.link is FakeRadioLink {
                FakeRadioButton()
            }
        }
        .tint(.appAccent)
    }
}

struct MainTabs: View {
    var body: some View {
        TabView {
            HomeView()
                .tabItem { Label("Radio", systemImage: "antenna.radiowaves.left.and.right") }
            SettingsView()
                .tabItem { Label("Settings", systemImage: "gearshape") }
            LogView()
                .tabItem { Label("Alert log", systemImage: "list.bullet.rectangle") }
        }
    }
}

extension Color {
    /// A blue dark enough for white text on prominent buttons (about 6.8:1).
    static let appAccent = Color(red: 0, green: 0.36, blue: 0.75)
}
