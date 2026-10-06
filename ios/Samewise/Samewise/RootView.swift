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
    }
}

struct MainTabs: View {
    var body: some View {
        TabView {
            HomeView()
                .tabItem { Label("Radio", systemImage: "antenna.radiowaves.left.and.right") }
        }
    }
}
