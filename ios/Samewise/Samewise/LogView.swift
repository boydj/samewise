import SamewiseKit
import SwiftUI

/// The radio's alert log, newest first, with event names from its event table.
struct LogView: View {
    @Environment(RadioSession.self) private var session

    var body: some View {
        NavigationStack {
            List {
                ProblemSection()
                if session.log.isEmpty {
                    Text("No alerts received yet.")
                        .foregroundStyle(.secondary)
                        .accessibilityIdentifier("log.empty")
                }
                ForEach(Array(session.log.enumerated()), id: \.offset) { i, entry in
                    let row = LogRow(entry, events: session.events, document: session.document, now: Date())
                    VStack(alignment: .leading, spacing: 4) {
                        Text(row.title)
                            .font(.headline)
                        Text(row.received)
                            .font(.subheadline)
                        Text(row.outcome)
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        if !row.locations.isEmpty {
                            Text(row.locations.map { CountyTable.shared.name($0) }.joined(separator: ", "))
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .accessibilityElement(children: .combine)
                    .accessibilityIdentifier("log.row\(i)")
                }
            }
            .navigationTitle("Alert log")
            .refreshable { await session.reloadAll() }
        }
    }
}
