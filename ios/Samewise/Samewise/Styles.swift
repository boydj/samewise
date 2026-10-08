import SwiftUI

/// A list section whose header is full-contrast body text that scales with
/// Dynamic Type (the system's grey small-caps header doesn't pass the
/// accessibility audit's contrast check).
struct TitledSection<Content: View>: View {
    let title: String
    let content: Content

    init(_ title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        Section {
            content
        } header: {
            SectionTitle(title)
        }
    }
}

struct SectionTitle: View {
    let title: String

    init(_ title: String) {
        self.title = title
    }

    var body: some View {
        Text(title)
            .font(.headline)
            .foregroundStyle(.primary)
            .textCase(nil)
            .accessibilityAddTraits(.isHeader)
    }
}

/// An explanation inside a section, as a row: full contrast, wraps, scales.
struct Note: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        Text(text)
            .font(.footnote)
    }
}

extension Color {
    /// A red dark enough for text on a light row (about 7:1), for warnings
    /// and destructive actions.
    static let appWarning = Color(red: 0.72, green: 0, blue: 0)
}
