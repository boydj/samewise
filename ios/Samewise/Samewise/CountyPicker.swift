import GattModel
import SamewiseKit
import SwiftUI

/// Choose SAME locations by county and state name. Advanced: part of a
/// county (a SAME subdivision) or a whole state.
struct CountyPicker: View {
    let title: String
    let save: ([String]) async -> Bool

    @State private var codes: [String]
    @State private var query = ""
    @State private var subdivision = 0
    @State private var advanced = false
    @Environment(\.dismiss) private var dismiss

    private let table = CountyTable.shared

    init(title: String, codes: [String], save: @escaping ([String]) async -> Bool) {
        self.title = title
        self.save = save
        _codes = State(initialValue: codes)
    }

    var body: some View {
        List {
            Section {
                ForEach(codes, id: \.self) { code in
                    Text(table.name(code))
                        .accessibilityIdentifier("counties.chosen.\(code)")
                }
                .onDelete { codes.remove(atOffsets: $0) }
                if codes.isEmpty {
                    Text("None yet. Search below.").foregroundStyle(.secondary)
                }
            } header: {
                Text("Chosen (\(codes.count) of \(Codec.maxCounties))")
            }
            if advanced {
                Section {
                    Picker("Part of the county", selection: $subdivision) {
                        ForEach(0..<CountyTable.subdivisions.count, id: \.self) { p in
                            Text(CountyTable.subdivisions[p]).tag(p)
                        }
                    }
                } footer: {
                    Text("Some offices warn for part of a county. Most people want All.")
                }
            }
            Section("Results") {
                if table.isEmpty {
                    Text("The county list is missing from this build.").foregroundStyle(.secondary)
                }
                ForEach(table.search(query), id: \.code) { c in
                    Button(c.title) { add(String(subdivision) + c.code.dropFirst()) }
                        .disabled(codes.count >= Codec.maxCounties)
                        .accessibilityIdentifier("counties.result.\(c.code)")
                }
                if advanced, let state = table.findState(query) {
                    Button("All of \(state.name)") { add("0" + state.fips + "000") }
                        .disabled(codes.count >= Codec.maxCounties)
                }
            }
        }
        .searchable(text: $query, placement: .navigationBarDrawer(displayMode: .always),
                    prompt: "County and state, like Travis, Texas")
        .navigationTitle(title)
        .toolbar {
            ToolbarItem(placement: .confirmationAction) {
                Button("Save") {
                    Task {
                        if await save(codes) { dismiss() }
                    }
                }
                .accessibilityIdentifier("counties.save")
            }
            ToolbarItem(placement: .secondaryAction) {
                Toggle("Advanced", isOn: $advanced)
            }
        }
    }

    private func add(_ code: String) {
        if !codes.contains(code) && codes.count < Codec.maxCounties {
            codes.append(code)
        }
    }
}
