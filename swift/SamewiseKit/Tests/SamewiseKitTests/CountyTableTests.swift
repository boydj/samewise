import XCTest

@testable import SamewiseKit

/// The table's logic on a small sample in the bundled file's format.
@MainActor
final class CountyTableTests: XCTestCase {
    let table = CountyTable(text: """
        # Source: sample for tests
        48|000||TX|Texas
        48|453|Travis County|TX|Texas
        48|029|Bexar County|TX|Texas
        22|071|Orleans Parish|LA|Louisiana
        51|760|Richmond city|VA|Virginia
        51|159|Richmond County|VA|Virginia
        54|039|Kanawha County|WV|West Virginia
        72|127|San Juan Municipio|PR|Puerto Rico
        02|110|Juneau City and Borough|AK|Alaska
        """)

    func testNamesForCodes() async {
        XCTAssertEqual(table.name("048453"), "Travis County, TX")
        XCTAssertEqual(table.name("148453"), "Northwest Travis County, TX")
        XCTAssertEqual(table.name("048000"), "All of Texas")
        XCTAssertEqual(table.name("072127"), "San Juan Municipio, PR")
        XCTAssertEqual(table.name("099999"), "099999", "unknown codes show as themselves")
        XCTAssertEqual(table.source, "sample for tests")
        XCTAssertEqual(table.county(code: "548453")?.code, "048453")
    }

    func testSearch() async {
        XCTAssertEqual(table.search("travis").map(\.code), ["048453"])
        XCTAssertEqual(table.search("richmond").count, 2)
        XCTAssertEqual(table.search("richmond, virginia").count, 2)
        XCTAssertEqual(table.search("orleans la").map(\.code), ["022071"])
        XCTAssertEqual(table.search("kanawha west virginia").map(\.code), ["054039"])
        XCTAssertEqual(table.search("san juan puerto rico").map(\.code), ["072127"])
        XCTAssertEqual(table.search("TRAVIS COUNTY").map(\.code), ["048453"])
        XCTAssertEqual(table.search("xar").map(\.code), ["048029"], "falls back to a substring")
        XCTAssertEqual(table.search(""), [])
    }

    func testMatchesReverseGeocodedPlaces() async {
        XCTAssertEqual(table.match(county: "Travis County", locality: "Austin", state: "TX")?.code, "048453")
        XCTAssertEqual(table.match(county: "Orleans Parish", locality: "New Orleans", state: "LA")?.code, "022071")
        XCTAssertEqual(table.match(county: nil, locality: "Richmond", state: "VA")?.code, "051760",
                       "an independent city has no county")
        XCTAssertEqual(table.match(county: "Juneau", locality: nil, state: "Alaska")?.code, "002110")
        XCTAssertNil(table.match(county: "Travis County", locality: nil, state: "LA"))
        XCTAssertNil(table.match(county: "Travis County", locality: nil, state: nil))
    }
}
