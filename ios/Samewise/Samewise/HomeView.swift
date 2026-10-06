import GattModel
import SamewiseKit
import SwiftUI

/// Status, live from the radio's notifications, and the Control commands.
struct HomeView: View {
    @Environment(RadioSession.self) private var session
    @State private var confirmingReset = false

    var body: some View {
        NavigationStack {
            List {
                ProblemSection()
                if let status = session.status {
                    let s = StatusSummary(status, document: session.document, now: Date())
                    if !s.warnings.isEmpty {
                        Section("Warnings") {
                            ForEach(s.warnings, id: \.name) { w in
                                Label(w.text, systemImage: "exclamationmark.triangle.fill")
                                    .foregroundStyle(.red)
                                    .accessibilityIdentifier("home.warning.\(w.name)")
                            }
                        }
                    }
                    Section("Radio") {
                        LabeledContent("Battery", value: s.battery)
                            .accessibilityIdentifier("home.battery")
                        LabeledContent("Time left", value: s.hoursLeft)
                        LabeledContent("Channel", value: s.channel)
                            .accessibilityIdentifier("home.channel")
                        LabeledContent("Signal", value: s.signal)
                        LabeledContent("Last weekly test", value: s.lastWeeklyTest)
                        LabeledContent("Keys", value: s.locked ? "Locked" : "Unlocked")
                    }
                }
                Section("Counties") {
                    if session.settings.home.isEmpty {
                        Text("None chosen. The radio alerts for no county until you add one.")
                            .foregroundStyle(.secondary)
                            .accessibilityIdentifier("home.noCounties")
                    }
                    ForEach(session.settings.home, id: \.self) { code in
                        Text(CountyTable.shared.name(code))
                            .accessibilityIdentifier("home.county.\(code)")
                    }
                    if session.settings.travelMode {
                        Label("Travel mode is on", systemImage: "car")
                    }
                }
                if let rule = session.timeRule, !rule.exact {
                    Section("Clock") {
                        Text("This time zone's daylight-saving rules can't be sent to the radio, so it has the current offset. It's refreshed every time this iPhone connects.")
                            .font(.footnote)
                    }
                }
                Section {
                    Button("Test alert") { Task { await session.send(.testAlert) } }
                        .accessibilityIdentifier("control.test")
                    Button("Clear alert log") { Task { await session.send(.clearLog) } }
                        .accessibilityIdentifier("control.clear")
                    Button("Factory reset", role: .destructive) { confirmingReset = true }
                        .accessibilityIdentifier("control.reset")
                    if let outcome = session.lastControl {
                        Text(outcome.text)
                            .font(.callout)
                            .accessibilityIdentifier("control.result")
                    }
                } header: {
                    Text("Control")
                } footer: {
                    Text("A factory reset clears every setting and the log, and the radio forgets every phone. You confirm it on the radio.")
                }
            }
            .navigationTitle("Radio")
            .toolbar {
                Button("Disconnect") { session.disconnect() }
                    .accessibilityIdentifier("home.disconnect")
            }
            .confirmationDialog("Reset the radio to factory settings?", isPresented: $confirmingReset,
                                titleVisibility: .visible) {
                Button("Reset", role: .destructive) { Task { await session.send(.factoryReset) } }
                    .accessibilityIdentifier("control.resetConfirm")
            } message: {
                Text("You'll confirm on the radio by long-pressing STBY.")
            }
        }
    }
}

/// The last problem, in plain language, with a way to dismiss it.
struct ProblemSection: View {
    @Environment(RadioSession.self) private var session

    var body: some View {
        if let problem = session.problem {
            Section {
                HStack(alignment: .top) {
                    Image(systemName: "exclamationmark.circle.fill")
                        .foregroundStyle(.orange)
                        .accessibilityHidden(true)
                    Text(problem.message)
                        .accessibilityIdentifier("problem")
                    Spacer()
                    Button("Dismiss") { session.dismissProblem() }
                        .buttonStyle(.borderless)
                        .accessibilityIdentifier("problem.dismiss")
                }
            }
        }
    }
}
