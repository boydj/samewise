import GattModel
import XCTest

@testable import SamewiseKit

@MainActor
final class SettingsModelTests: XCTestCase {
    var events: [Codec.Event] {
        GattDocument.bundled.defaultEventTable.entries.map {
            Codec.Event(code: $0.code, eventClass: UInt8($0.eventClass), name: $0.name)
        }
    }

    func testCustomFilterBitmapMatchesTheFirmwaresBitOrder() async {
        let e = events
        let b = CustomFilter.bitmap(codes: [e[0].code, e[9].code], events: e)
        XCTAssertEqual(b.count, Codec.filterBytes)
        XCTAssertEqual(b[0], 0b0000_0001)
        XCTAssertEqual(b[1], 0b0000_0010, "index 9 is byte 1, bit 1")
        XCTAssertEqual(CustomFilter.codes(bitmap: b, events: e), [e[0].code, e[9].code])
    }

    func testPresetsStartACustomList() async {
        let warnings = CustomFilter.codes(for: .warnings, events: events)
        XCTAssertTrue(warnings.contains("TOR"))
        XCTAssertFalse(warnings.contains("TOA"))
        XCTAssertTrue(CustomFilter.codes(for: .warningsAndWatches, events: events).contains("TOA"))
        XCTAssertFalse(CustomFilter.choosable(events).contains { $0.code == "RWT" }, "tests never alert")
    }

    func testCustomFilterRoundTripsThroughTheRadio() async {
        let session = RadioSession(link: FakeRadioLink())
        session.connect()
        await session.settled()
        let chosen: Set<String> = ["TOR", "SVR", "FFW"]
        let ok = await session.setFilter(preset: FilterPreset.custom.rawValue,
                                         bitmap: CustomFilter.bitmap(codes: chosen, events: session.events))
        XCTAssertTrue(ok)
        XCTAssertEqual(session.settings.filterPreset, FilterPreset.custom.rawValue)
        XCTAssertEqual(CustomFilter.codes(bitmap: session.settings.filterBitmap, events: session.events), chosen)
    }

    func testPresetText() async {
        XCTAssertEqual(PresetText.parse(band: .fm, "101.1"), Codec.Preset(band: 0, khz: 101_100))
        XCTAssertEqual(PresetText.parse(band: .fm, "101,1"), Codec.Preset(band: 0, khz: 101_100))
        XCTAssertEqual(PresetText.parse(band: .am, "1200"), Codec.Preset(band: 1, khz: 1200))
        XCTAssertEqual(PresetText.parse(band: .wb, "162.475"), Codec.Preset(band: 2, khz: 162_475))
        XCTAssertNil(PresetText.parse(band: .wb, "162.41"), "off the 25 kHz grid")
        XCTAssertNil(PresetText.parse(band: .fm, "120"))
        XCTAssertNil(PresetText.parse(band: .am, "abc"))
        XCTAssertEqual(PresetText.frequency(Codec.Preset(band: 0, khz: 101_100)), "101.1 MHz")
        XCTAssertEqual(PresetText.frequency(Codec.Preset(band: 1, khz: 1200)), "1200 kHz")
        XCTAssertEqual(PresetText.frequency(Codec.Preset(band: 2, khz: 162_550)), "162.550 MHz")
    }

    func testWeatherChannels() async {
        XCTAssertEqual(WeatherChannel.title(0), "Automatic")
        XCTAssertEqual(WeatherChannel.title(1), "WX1, 162.550 MHz")
        XCTAssertEqual(WeatherChannel.title(7), "WX7, 162.525 MHz")
        for khz in WeatherChannel.khz {
            XCTAssertTrue(Codec.presetValid(Codec.Preset(band: Codec.Band.wb.rawValue, khz: khz)))
        }
    }
}
