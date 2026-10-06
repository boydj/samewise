import SamewiseKit
import SwiftUI

/// Travel mode: the radio alerts for the travel counties instead of home.
/// "Use my location" sets them from the county you're in, and the app
/// refreshes them on every connection while travel mode is on.
struct TravelSection: View {
    @Environment(RadioSession.self) private var session
    @Environment(AppServices.self) private var services
    @State private var locating = false
    @State private var locationProblem: LocationError?

    var body: some View {
        Section {
            Toggle("Travel mode", isOn: Binding(
                get: { session.settings.travelMode },
                set: { on in
                    Task {
                        await session.setMode(travel: on, channel: session.settings.channel)
                        if on && session.settings.travel.isEmpty { await locate() }
                    }
                }
            ))
            .accessibilityIdentifier("travel.toggle")
            ForEach(session.settings.travel, id: \.self) { code in
                Text(CountyTable.shared.name(code))
                    .accessibilityIdentifier("travel.county.\(code)")
            }
            Button {
                Task { await locate() }
            } label: {
                HStack {
                    Label("Use my location", systemImage: "location")
                    if locating {
                        Spacer()
                        ProgressView()
                    }
                }
            }
            .disabled(locating)
            .accessibilityIdentifier("travel.locate")
            NavigationLink("Choose travel counties") {
                CountyPicker(title: "Travel counties", codes: session.settings.travel) { codes in
                    await session.setTravelCounties(codes)
                }
            }
            if let locationProblem {
                Text(locationProblem.message)
                    .font(.footnote)
                    .foregroundStyle(.orange)
                    .accessibilityIdentifier("travel.problem")
            }
        } header: {
            Text("Travel")
        } footer: {
            Text("In travel mode the radio alerts for the travel counties instead of your home counties. While it's on, this iPhone updates them each time it connects.")
        }
    }

    private func locate() async {
        locating = true
        locationProblem = await session.useCurrentLocation(services.location)
        locating = false
    }
}
