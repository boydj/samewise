import XCTest

@testable import SamewiseKit

@MainActor
final class TravelTests: XCTestCase {
    let table = CountyTable(text: """
        48|000||TX|Texas
        48|453|Travis County|TX|Texas
        48|029|Bexar County|TX|Texas
        """)
    var link: FakeRadioLink!
    var session: RadioSession!

    override func setUp() async throws {
        link = FakeRadioLink()
        session = RadioSession(link: link)
        session.connect()
        await session.settled()
    }

    func testUseMyLocationWritesTheTravelCounty() async {
        let lookup = FakeLocationLookup(Place(county: "Travis County", locality: "Austin", state: "TX"))
        let problem = await session.useCurrentLocation(lookup, table: table)
        XCTAssertNil(problem)
        XCTAssertEqual(link.radio.travel, ["048453"])
        XCTAssertEqual(session.settings.travel, ["048453"])
    }

    func testPlacesTheTableDoesntKnow() async {
        var problem = await session.useCurrentLocation(FakeLocationLookup(nil), table: table)
        XCTAssertEqual(problem, .unavailable)
        problem = await session.useCurrentLocation(
            FakeLocationLookup(Place(county: "Hennepin County", locality: nil, state: "MN")), table: table)
        XCTAssertEqual(problem, .notACounty("Hennepin County, MN"))
        XCTAssertEqual(link.radio.travel, [], "nothing written")
    }

    func testRefreshesOnConnectOnlyInTravelMode() async {
        let lookup = FakeLocationLookup(Place(county: "Bexar County", locality: "San Antonio", state: "TX"))
        session.onConnect = [session.travelRefreshStep(lookup, table: table)]
        session.disconnect()
        link.window = .connect
        session.connect()
        await session.settled()
        XCTAssertEqual(lookup.lookups, 0, "home mode: no lookup")

        await session.setMode(travel: true, channel: 0)
        session.disconnect()
        link.window = .connect
        session.connect()
        await session.settled()
        XCTAssertEqual(lookup.lookups, 1)
        XCTAssertEqual(link.radio.travel, ["048029"])
    }

    func testFakeLocationArgument() async {
        let o = LaunchOptions(arguments: ["x", "-fakeLocation", "Travis County|Austin|TX"], simulator: true)
        XCTAssertEqual(o.fakePlace, Place(county: "Travis County", locality: "Austin", state: "TX"))
        XCTAssertEqual(Place(argument: "|Richmond|VA"), Place(county: nil, locality: "Richmond", state: "VA"))
        XCTAssertNil(Place(argument: "nope"))
    }
}
