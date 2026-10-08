import XCTest

@testable import SamewiseKit

/// The bundled Census table (Resources/counties.txt): names to SAME codes
/// and back, territories included.
@MainActor
final class BundledCountyTableTests: XCTestCase {
    let table = CountyTable.shared

    func testTheTableIsBundledAndCitesTheCensus() async {
        XCTAssertGreaterThan(table.counties.count, 3200)
        XCTAssertTrue(table.source.contains("Census"), table.source)
    }

    func testNamesToCodesAndBack() async {
        let cases: [(query: String, code: String, name: String)] = [
            ("Travis, Texas", "048453", "Travis County, TX"),
            ("Orleans, Louisiana", "022071", "Orleans Parish, LA"),
            ("District of Columbia", "011001", "District of Columbia, DC"),
            ("San Juan, Puerto Rico", "072127", "San Juan Municipio, PR"),
            ("Guam", "066010", "Guam, GU"),
            ("St. Thomas, Virgin Islands", "078030", "St. Thomas Island, VI"),
            ("Eastern, American Samoa", "060010", "Eastern District, AS"),
            ("Saipan, Northern Mariana Islands", "069110", "Saipan Municipality, MP"),
        ]
        for c in cases {
            let found = table.search(c.query).map(\.code)
            XCTAssertTrue(found.contains(c.code), "\(c.query) -> \(found)")
            XCTAssertEqual(table.name(c.code), c.name, c.code)
        }
        XCTAssertEqual(table.findState("Virgin Islands")?.postal, "VI")
        XCTAssertEqual(table.findState("Northern Mariana Islands")?.postal, "MP")
        XCTAssertEqual(table.findState("Virginia")?.postal, "VA", "an exact name wins over a longer one")
    }

    func testEveryCountyRoundTrips() async {
        for c in table.counties {
            XCTAssertEqual(table.county(code: c.code), c, c.code)
            XCTAssertEqual(table.name(c.code), c.title, c.code)
        }
        XCTAssertEqual(table.name("048000"), "All of Texas")
        XCTAssertEqual(table.name("072000"), "All of Puerto Rico")
    }
}
