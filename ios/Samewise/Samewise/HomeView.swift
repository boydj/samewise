import SamewiseKit
import SwiftUI

struct HomeView: View {
    @Environment(RadioSession.self) private var session

    var body: some View {
        NavigationStack {
            List {
                if let status = session.status {
                    LabeledContent("Battery", value: "\(status.batteryPercent)%")
                        .accessibilityIdentifier("home.battery")
                }
            }
            .navigationTitle("Radio")
            .toolbar {
                Button("Disconnect") { session.disconnect() }
                    .accessibilityIdentifier("home.disconnect")
            }
        }
    }
}
