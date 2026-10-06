import Foundation

/// A place as a reverse geocoder names it.
public struct Place: Equatable, Sendable {
    /// "Travis County" (CLPlacemark.subAdministrativeArea).
    public var county: String?
    /// "Austin" (CLPlacemark.locality); an independent city's only name.
    public var locality: String?
    /// "TX" or "Texas" (CLPlacemark.administrativeArea).
    public var state: String?

    public init(county: String?, locality: String?, state: String?) {
        self.county = county
        self.locality = locality
        self.state = state
    }

    /// "Travis County|Austin|TX", as the -fakeLocation launch argument writes it.
    public init?(argument: String) {
        let f = argument.split(separator: "|", omittingEmptySubsequences: false).map(String.init)
        guard f.count == 3 else { return nil }
        county = f[0].isEmpty ? nil : f[0]
        locality = f[1].isEmpty ? nil : f[1]
        state = f[2].isEmpty ? nil : f[2]
    }
}

public enum LocationError: Error, Equatable, Sendable {
    case denied
    case unavailable
    /// The place isn't a U.S. county or equivalent the table knows.
    case notACounty(String)

    public var message: String {
        switch self {
        case .denied:
            return "Location access is off for WX Radio. Allow it in Settings, or choose travel counties yourself."
        case .unavailable:
            return "Couldn't find where you are. Try again outdoors, or choose travel counties yourself."
        case .notACounty(let place):
            return "\(place) isn't in a U.S. county the radio knows. Choose travel counties yourself."
        }
    }
}

/// Where the phone is. CoreLocation on a phone; a fixed place in tests.
@MainActor
public protocol LocationLookup: AnyObject {
    func currentPlace() async throws -> Place
}

@MainActor
public final class FakeLocationLookup: LocationLookup {
    public var place: Place?
    public private(set) var lookups = 0

    public init(_ place: Place?) {
        self.place = place
    }

    public func currentPlace() async throws -> Place {
        lookups += 1
        guard let place else { throw LocationError.unavailable }
        return place
    }
}

extension RadioSession {
    /// Travel mode's "use my location": the county the phone is in becomes
    /// the travel county list. Returns the problem, if any.
    @discardableResult
    public func useCurrentLocation(_ lookup: LocationLookup, table: CountyTable = .shared) async -> LocationError? {
        let place: Place
        do {
            place = try await lookup.currentPlace()
        } catch let e as LocationError {
            return e
        } catch {
            return .unavailable
        }
        guard let county = table.match(county: place.county, locality: place.locality, state: place.state) else {
            let name = [place.county ?? place.locality, place.state].compactMap { $0 }.joined(separator: ", ")
            return .notACounty(name.isEmpty ? "This place" : name)
        }
        return await setTravelCounties([county.code]) ? nil : .unavailable
    }

    /// On every connection while travel mode is on, follow the phone.
    public func travelRefreshStep(_ lookup: LocationLookup, table: CountyTable = .shared)
        -> (RadioSession) async -> Void {
        { session in
            if session.settings.travelMode {
                await session.useCurrentLocation(lookup, table: table)
            }
        }
    }
}
