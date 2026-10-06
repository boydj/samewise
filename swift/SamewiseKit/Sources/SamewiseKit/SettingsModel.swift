import Foundation
import GattModel

/// The alert filter: a preset, or a custom list over the radio's event table.
public enum FilterPreset: UInt8, CaseIterable, Sendable {
    case warnings = 0
    case warningsAndWatches = 1
    case all = 2
    case custom = 3

    public var title: String {
        switch self {
        case .warnings: return "Warnings"
        case .warningsAndWatches: return "Warnings and watches"
        case .all: return "Everything"
        case .custom: return "Custom"
        }
    }

    public var detail: String {
        switch self {
        case .warnings: return "Only warnings sound an alert."
        case .warningsAndWatches: return "Warnings and watches sound an alert. The radio's default."
        case .all: return "Warnings, watches, advisories and statements sound an alert."
        case .custom: return "Only the events you choose sound an alert."
        }
    }

    /// Event classes (the firmware's enum event_class) a preset alerts on.
    public var classes: Set<UInt8> {
        switch self {
        case .warnings: return [EventClass.warning]
        case .warningsAndWatches: return [EventClass.warning, EventClass.watch]
        case .all: return [EventClass.warning, EventClass.watch, EventClass.advisory, EventClass.statement]
        case .custom: return []
        }
    }
}

/// The firmware's enum event_class.
public enum EventClass {
    public static let warning: UInt8 = 0
    public static let watch: UInt8 = 1
    public static let advisory: UInt8 = 2
    public static let statement: UInt8 = 3
    /// Tests never alert under any filter; RWT feeds the weekly-test check.
    public static let test: UInt8 = 4

    public static func name(_ c: UInt8) -> String {
        ["Warning", "Watch", "Advisory", "Statement", "Test"][Int(min(c, 4))]
    }
}

public enum CustomFilter {
    /// One bit per event table index: bit i is byte i / 8, bit i % 8.
    public static func bitmap(codes: Set<String>, events: [Codec.Event]) -> [UInt8] {
        var b = [UInt8](repeating: 0, count: Codec.filterBytes)
        for (i, e) in events.enumerated() where codes.contains(e.code) && i < Codec.filterBytes * 8 {
            b[i / 8] |= 1 << UInt8(i % 8)
        }
        return b
    }

    public static func codes(bitmap: [UInt8], events: [Codec.Event]) -> Set<String> {
        var codes: Set<String> = []
        for (i, e) in events.enumerated() where i / 8 < bitmap.count && bitmap[i / 8] & (1 << UInt8(i % 8)) != 0 {
            codes.insert(e.code)
        }
        return codes
    }

    /// The codes a preset alerts on, to start a custom list from.
    public static func codes(for preset: FilterPreset, events: [Codec.Event]) -> Set<String> {
        Set(events.filter { preset.classes.contains($0.eventClass) }.map(\.code))
    }

    /// Events a custom list can choose: tests never alert.
    public static func choosable(_ events: [Codec.Event]) -> [Codec.Event] {
        events.filter { $0.eventClass != EventClass.test }
    }
}

/// NOAA Weather Radio channels.
public enum WeatherChannel {
    /// kHz for channels 1-7.
    public static let khz: [UInt32] = [162_550, 162_400, 162_475, 162_425, 162_450, 162_500, 162_525]

    public static func title(_ channel: UInt8) -> String {
        guard (1...7).contains(channel) else { return "Automatic" }
        return "WX\(channel), " + PresetText.frequency(Codec.Preset(band: Codec.Band.wb.rawValue,
                                                                     khz: khz[Int(channel) - 1]))
    }
}

/// Station presets as people write them.
public enum PresetText {
    public static func bandName(_ band: UInt8) -> String {
        switch Codec.Band(rawValue: band) {
        case .fm: return "FM"
        case .am: return "AM"
        case .wb: return "Weather"
        case nil: return "?"
        }
    }

    /// "101.1 MHz", "1200 kHz", "162.550 MHz".
    public static func frequency(_ p: Codec.Preset) -> String {
        switch Codec.Band(rawValue: p.band) {
        case .am: return "\(p.khz) kHz"
        case .fm: return String(format: "%.1f MHz", Double(p.khz) / 1000)
        case .wb: return String(format: "%.3f MHz", Double(p.khz) / 1000)
        case nil: return "\(p.khz) kHz"
        }
    }

    /// A preset from typed text: MHz for FM and weather, kHz for AM. Nil if
    /// the radio would refuse it.
    public static func parse(band: Codec.Band, _ text: String) -> Codec.Preset? {
        let t = text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
        guard let v = Double(t), v > 0 else { return nil }
        let khz: Double = band == .am ? v : v * 1000
        let p = Codec.Preset(band: band.rawValue, khz: UInt32(khz.rounded()))
        return Codec.presetValid(p) ? p : nil
    }

    public static func hint(_ band: Codec.Band) -> String {
        switch band {
        case .fm: return "87.5 to 108.0 MHz"
        case .am: return "520 to 1710 kHz"
        case .wb: return "162.400 to 162.550 MHz, in 25 kHz steps"
        }
    }
}
