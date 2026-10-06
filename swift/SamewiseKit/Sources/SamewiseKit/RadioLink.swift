import GattModel

/// A characteristic of the radio's settings service, named as in docs/gatt.json.
public enum Chr: String, CaseIterable, Sendable {
    case counties
    case travelCounties = "travel_counties"
    case mode
    case eventFilter = "event_filter"
    case eventTable = "event_table"
    case time
    case presets
    case status
    case alertLog = "alert_log"
    case control

    /// The ones the radio notifies or indicates.
    public static let subscribed: [Chr] = [.status, .alertLog, .control]
}

public enum LinkState: Equatable, Sendable {
    /// Not looking for the radio.
    case idle
    /// Scanning. The radio advertises only while a window is open on it.
    case searching
    /// Found it: connecting, discovering, subscribing (and pairing, the first time).
    case connecting
    case connected
}

/// Why Bluetooth itself can't be used.
public enum BluetoothProblem: Equatable, Sendable {
    case off
    case unauthorized
    case unsupported
}

public enum RadioError: Error, Equatable, Sendable {
    /// The radio refused: an ATT error code (docs/gatt.json, or a standard one).
    case att(UInt8)
    case bluetooth(BluetoothProblem)
    /// The phone holds a bond the radio no longer has (it was reset or replaced
    /// this phone). The phone must forget the radio in iOS Settings and pair again.
    case staleBond
    /// Pairing didn't complete: the passkey was wrong or the prompt was cancelled.
    case pairingFailed
    case notConnected
    /// The link dropped while an operation was in flight.
    case disconnected
    case timeout

    public init(_ e: AttError) {
        self = .att(e.code)
    }
}

/// Something the link reports on its own.
public enum LinkEvent: Equatable, Sendable {
    case state(LinkState)
    /// A notification or indication.
    case value(Chr, [UInt8])
    /// The link failed outside a read or write (pairing, a dropped connection).
    case failed(RadioError)
}

/// The app's only way to the radio. CoreBluetooth for real use; FakeRadioLink
/// (on GattModel's RadioState) for tests, previews and the simulator.
@MainActor
public protocol RadioLink: AnyObject {
    var state: LinkState { get }
    /// Set by the session that owns the link.
    var onEvent: (LinkEvent) -> Void { get set }

    /// Scan for the settings service and connect to the radio when it
    /// advertises; .connected once subscribed to every notifying characteristic.
    func startSearching()
    func stopSearching()
    func disconnect()

    func read(_ chr: Chr) async throws -> [UInt8]
    func write(_ chr: Chr, _ value: [UInt8]) async throws
}
