#if canImport(CoreLocation)
import CoreLocation
import Foundation

/// The phone's county: one location fix with When In Use permission, then
/// CLGeocoder's reverse geocode.
@MainActor
public final class CoreLocationLookup: NSObject, LocationLookup {
    private let manager = CLLocationManager()
    private var authorization: CheckedContinuation<CLAuthorizationStatus, Never>?
    private var fix: CheckedContinuation<CLLocation, Error>?

    public override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyKilometer
    }

    public func currentPlace() async throws -> Place {
        var status = manager.authorizationStatus
        if status == .notDetermined {
            status = await withCheckedContinuation { cont in
                authorization = cont
                manager.requestWhenInUseAuthorization()
            }
        }
        // .authorizedWhenInUse doesn't exist on macOS; anything but these is allowed.
        guard status != .denied && status != .restricted && status != .notDetermined else {
            throw LocationError.denied
        }
        let location = try await withCheckedThrowingContinuation { cont in
            fix = cont
            manager.requestLocation()
        }
        guard let p = try await CLGeocoder().reverseGeocodeLocation(location).first,
              p.isoCountryCode == nil || ["US", "PR", "GU", "VI", "AS", "MP"].contains(p.isoCountryCode!)
        else { throw LocationError.notACounty("This place") }
        return Place(county: p.subAdministrativeArea, locality: p.locality,
                     state: p.isoCountryCode == "US" || p.isoCountryCode == nil ? p.administrativeArea : p.isoCountryCode)
    }

    private func authorizationChanged(_ status: CLAuthorizationStatus) {
        guard status != .notDetermined, let cont = authorization else { return }
        authorization = nil
        cont.resume(returning: status)
    }

    private func located(_ result: Result<CLLocation, Error>) {
        guard let cont = fix else { return }
        fix = nil
        cont.resume(with: result)
    }
}

extension CoreLocationLookup: CLLocationManagerDelegate {
    nonisolated public func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        let status = manager.authorizationStatus
        MainActor.assumeIsolated { authorizationChanged(status) }
    }

    nonisolated public func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let last = locations.last else { return }
        MainActor.assumeIsolated { located(.success(last)) }
    }

    nonisolated public func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        let denied = (error as? CLError)?.code == .denied
        MainActor.assumeIsolated { located(.failure(denied ? LocationError.denied : LocationError.unavailable)) }
    }
}
#endif
