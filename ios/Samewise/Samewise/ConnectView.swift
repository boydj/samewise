import SamewiseKit
import SwiftUI

struct ConnectView: View {
    @Environment(RadioSession.self) private var session

    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                Text(session.linkState == .searching ? "Searching for your radio" : "Not connected")
                    .font(.title2)
                    .accessibilityIdentifier("connect.state")
                if session.linkState == .idle {
                    Button("Connect") { session.connect() }
                        .buttonStyle(.borderedProminent)
                        .accessibilityIdentifier("connect.start")
                } else {
                    ProgressView()
                    Button("Stop") { session.link.stopSearching() }
                        .accessibilityIdentifier("connect.stop")
                }
            }
            .padding()
            .navigationTitle("WX Radio")
        }
    }
}
