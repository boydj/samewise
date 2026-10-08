import Foundation
import GattModel

/// How the app was started: the real radio, or the fake one for the
/// simulator (which has no Bluetooth), UI tests and demos.
///
///   -fakeRadio            use FakeRadioLink (always on in the simulator)
///   -fakeWindow none|connect|pairing   the fake radio's window at launch (pairing)
///   -fakeBonded           the fake radio already knows this phone
///   -fakeLocation "Travis County|Austin|TX"   where travel mode finds the phone
public struct LaunchOptions: Equatable {
    public var fake: Bool
    public var window: FakeRadioLink.Window
    public var bonded: Bool
    public var fakePlace: Place?

    public init(arguments: [String], simulator: Bool) {
        fake = simulator || arguments.contains("-fakeRadio")
        bonded = arguments.contains("-fakeBonded")
        window = .pairing
        if let i = arguments.firstIndex(of: "-fakeLocation"), i + 1 < arguments.count {
            fakePlace = Place(argument: arguments[i + 1])
        }
        if let i = arguments.firstIndex(of: "-fakeWindow"), i + 1 < arguments.count {
            switch arguments[i + 1] {
            case "none": window = .none
            case "connect": window = .connect
            default: window = .pairing
            }
        }
    }

    /// Where travel mode looks: the fake place, or CoreLocation.
    @MainActor
    public func makeLocationLookup() -> LocationLookup {
        if let fakePlace {
            return FakeLocationLookup(fakePlace)
        }
        #if canImport(CoreLocation)
        return CoreLocationLookup()
        #else
        return FakeLocationLookup(nil)
        #endif
    }

    /// The link these options ask for.
    @MainActor
    public func makeLink() -> RadioLink {
        if fake {
            return FakeRadioLink(window: window, bonded: bonded)
        }
        #if canImport(CoreBluetooth)
        return CoreBluetoothLink()
        #else
        return FakeRadioLink(window: window, bonded: bonded)
        #endif
    }
}
