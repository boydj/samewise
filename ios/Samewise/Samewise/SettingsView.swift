import GattModel
import SamewiseKit
import SwiftUI

/// Counties, travel mode, channel, filter and presets. Every change is
/// written at once; the screen then shows what the radio holds.
struct SettingsView: View {
    @Environment(RadioSession.self) private var session

    var body: some View {
        NavigationStack {
            Form {
                ProblemSection()
                TitledSection("Home counties") {
                    ForEach(session.settings.home, id: \.self) { code in
                        Text(CountyTable.shared.name(code))
                    }
                    NavigationLink("Choose counties") {
                        CountyPicker(title: "Home counties", codes: session.settings.home) { codes in
                            await session.setHomeCounties(codes)
                        }
                    }
                    .accessibilityIdentifier("settings.homeCounties")
                }
                TravelSection()
                TitledSection("Weather channel") {
                    Picker("Channel", selection: Binding(
                        get: { session.settings.channel },
                        set: { c in Task { await session.setMode(travel: session.settings.travelMode, channel: c) } }
                    )) {
                        ForEach(UInt8(0)...UInt8(7), id: \.self) { c in
                            Text(WeatherChannel.title(c)).tag(c)
                        }
                    }
                    .pickerStyle(.navigationLink)  // the value wraps; a menu's label clips
                    .accessibilityIdentifier("settings.channel")
                }
                TitledSection("Alerts") {
                    NavigationLink {
                        FilterView()
                    } label: {
                        LabeledContent("Alert for",
                                       value: FilterPreset(rawValue: session.settings.filterPreset)?.title ?? "")
                    }
                    .accessibilityIdentifier("settings.filter")
                }
                PresetsSection()
            }
            .navigationTitle("Settings")
        }
    }
}

struct FilterView: View {
    @Environment(RadioSession.self) private var session

    private var preset: FilterPreset { FilterPreset(rawValue: session.settings.filterPreset) ?? .warningsAndWatches }
    private var chosen: Set<String> {
        CustomFilter.codes(bitmap: session.settings.filterBitmap, events: session.events)
    }

    var body: some View {
        Form {
            ProblemSection()
            Section {
                Picker("Alert for", selection: Binding(get: { preset }, set: { choose($0) })) {
                    ForEach(FilterPreset.allCases, id: \.self) { p in
                        Text(p.title).tag(p)
                    }
                }
                .pickerStyle(.inline)
                .labelsHidden()
                .accessibilityIdentifier("filter.preset")
                Note(preset.detail + " Tests never sound an alert; they're logged.")
            }
            if preset == .custom {
                TitledSection("Events") {
                    ForEach(CustomFilter.choosable(session.events), id: \.code) { e in
                        Toggle(isOn: Binding(get: { chosen.contains(e.code) }, set: { toggle(e.code, $0) })) {
                            VStack(alignment: .leading) {
                                Text(e.name)
                                Text(EventClass.name(e.eventClass))
                                    .font(.caption)
                                    
                            }
                        }
                        .accessibilityIdentifier("filter.event.\(e.code)")
                    }
                }
            }
        }
        .navigationTitle("Alerts")
    }

    private func choose(_ p: FilterPreset) {
        // A new custom list starts from what the current preset alerts on.
        let bitmap = p == .custom && preset != .custom
            ? CustomFilter.bitmap(codes: CustomFilter.codes(for: preset, events: session.events),
                                  events: session.events)
            : session.settings.filterBitmap
        Task { await session.setFilter(preset: p.rawValue, bitmap: bitmap) }
    }

    private func toggle(_ code: String, _ on: Bool) {
        var codes = chosen
        if on { codes.insert(code) } else { codes.remove(code) }
        let bitmap = CustomFilter.bitmap(codes: codes, events: session.events)
        Task { await session.setFilter(preset: FilterPreset.custom.rawValue, bitmap: bitmap) }
    }
}

struct PresetsSection: View {
    @Environment(RadioSession.self) private var session
    @State private var band = Codec.Band.fm
    @State private var text = ""
    @State private var invalid: String?

    private var presets: [Codec.Preset] { session.settings.presets }

    var body: some View {
        Section {
            ForEach(Array(presets.enumerated()), id: \.offset) { i, p in
                LabeledContent(PresetText.bandName(p.band), value: PresetText.frequency(p))
                    .accessibilityIdentifier("presets.row\(i)")
            }
            .onDelete { offsets in
                var list = presets
                list.remove(atOffsets: offsets)
                Task { await session.setPresets(list) }
            }
            if presets.count < Codec.maxPresets {
                Picker("Band", selection: $band) {
                    Text("FM").tag(Codec.Band.fm)
                    Text("AM").tag(Codec.Band.am)
                    Text("Weather").tag(Codec.Band.wb)
                }
                .pickerStyle(.segmented)
                HStack {
                    TextField(PresetText.hint(band), text: $text)
                        .keyboardType(.decimalPad)
                        .accessibilityIdentifier("presets.frequency")
                    // Always enabled (a disabled button's grey text fails the
                    // contrast check); an invalid frequency is explained instead.
                    Button("Add") { add() }
                        .accessibilityIdentifier("presets.add")
                }
                if let invalid {
                    Text(invalid)
                        .font(.footnote)
                        .foregroundStyle(Color.appWarning)
                        .accessibilityIdentifier("presets.invalid")
                }
            }
            Note("Up to \(Codec.maxPresets). \(PresetText.hint(band)).")
        } header: {
            SectionTitle("Presets")
        }
    }

    private func add() {
        guard let p = PresetText.parse(band: band, text) else {
            invalid = "Type a frequency from \(PresetText.hint(band))."
            return
        }
        invalid = nil
        text = ""
        Task { await session.setPresets(presets + [p]) }
    }
}
