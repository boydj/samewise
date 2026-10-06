import SamewiseKit
import SwiftUI

/// Pairing and reconnecting. The radio advertises only while a window is
/// open on it, so the screen says which buttons to press; iOS shows its own
/// passkey prompt on the first encrypted access.
struct ConnectView: View {
    @Environment(RadioSession.self) private var session
    /// Set once this phone has connected; UI tests override it with
    /// `-pairedBefore NO` in the launch arguments.
    @AppStorage("pairedBefore") private var pairedBefore = false

    private var advice: ConnectAdvice {
        ConnectAdvice.make(state: session.linkState, problem: session.problem, pairedBefore: pairedBefore)
    }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    Text(advice.title)
                        .font(.title2.bold())
                        .accessibilityIdentifier("connect.title")
                    ForEach(Array(advice.steps.enumerated()), id: \.offset) { i, step in
                        HStack(alignment: .firstTextBaseline, spacing: 12) {
                            Text("\(i + 1)")
                                .font(.headline)
                                .accessibilityHidden(true)
                            Text(step)
                                .fixedSize(horizontal: false, vertical: true)  // wrap, never truncate
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .accessibilityIdentifier("connect.step\(i + 1)")
                        }
                    }
                    if session.linkState == .searching || session.linkState == .connecting {
                        HStack(spacing: 12) {
                            ProgressView()
                            Text(session.linkState == .searching ? "Searching" : "Connecting")
                                .foregroundStyle(.secondary)
                        }
                        .accessibilityElement(children: .combine)
                        Button("Stop") { session.link.stopSearching() }
                            .accessibilityIdentifier("connect.stop")
                    }
                    if let action = advice.action {
                        Button(action) { session.connect() }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.large)
                            .accessibilityIdentifier("connect.start")
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding()
            }
            .navigationTitle("WX Radio")
        }
        .onChange(of: session.linkState) { _, state in
            if state == .connected {
                pairedBefore = true
            }
        }
    }
}
