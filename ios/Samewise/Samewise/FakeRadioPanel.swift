import GattModel
import SamewiseKit
import SwiftUI

/// The fake radio's buttons, for the simulator and UI tests: what you would
/// press on the radio, and events the radio would see.
struct FakeRadioButton: View {
    @State private var showing = false

    var body: some View {
        Button {
            showing = true
        } label: {
            Label("Simulated radio", systemImage: "dot.radiowaves.left.and.right")
                .font(.footnote)
        }
        .buttonStyle(.borderedProminent)
        .padding(.bottom, 60)
        .accessibilityIdentifier("fake.open")
        .sheet(isPresented: $showing) { FakeRadioPanel() }
    }
}

struct FakeRadioPanel: View {
    @Environment(RadioSession.self) private var session
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        if let link = session.link as? FakeRadioLink {
            NavigationStack {
                List {
                    Section("Buttons") {
                        Button("Hold BAND (connect window)") { link.window = .connect }
                            .accessibilityIdentifier("fake.connectWindow")
                        Button("BAND + STBY (pairing window)") { link.window = .pairing }
                            .accessibilityIdentifier("fake.pairingWindow")
                        Button("Long-press STBY (confirm)") { link.radio.confirm() }
                            .accessibilityIdentifier("fake.confirm")
                        Button("Any other key (cancel)") { link.radio.cancel() }
                            .accessibilityIdentifier("fake.cancel")
                    }
                    Section("Events") {
                        Button("Tornado warning for 048453") { link.radio.injectAlert() }
                            .accessibilityIdentifier("fake.alert")
                        Button("Battery 15%") { link.radio.setBattery(15) }
                            .accessibilityIdentifier("fake.battery")
                        Button("Radio forgets this phone") { link.radioForgetsPhone() }
                            .accessibilityIdentifier("fake.forgetPhone")
                        Button("Out of range") { link.dropLink() }
                            .accessibilityIdentifier("fake.drop")
                    }
                }
                .navigationTitle("Simulated radio")
                .toolbar {
                    Button("Done") { dismiss() }
                        .accessibilityIdentifier("fake.done")
                }
            }
        }
    }
}
