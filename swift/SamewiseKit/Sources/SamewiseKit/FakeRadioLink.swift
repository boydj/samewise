import Foundation
import GattModel

/// An in-memory radio for unit tests, UI tests, previews and the simulator
/// (which has no Bluetooth). The radio's side is GattModel's RadioState,
/// the same model the Mac mock serves, plus the connection policy the mock
/// can't show: advertising only in a window, bonded phones only in the
/// connect window, and a bond the radio can forget.
@MainActor
public final class FakeRadioLink: RadioLink {
    public enum Window: Sendable {
        case none
        /// Long-press BAND: bonded phones only.
        case connect
        /// BAND + STBY: a new phone may pair.
        case pairing
    }

    public let radio: RadioState
    public private(set) var state: LinkState = .idle {
        didSet {
            if state != oldValue {
                onEvent(.state(state))
            }
        }
    }
    public var onEvent: (LinkEvent) -> Void = { _ in }

    /// The radio's window; opening one while the phone searches connects it.
    public var window: Window {
        didSet { attempt() }
    }
    /// Whether the radio holds this phone's bond, and whether the phone holds the radio's.
    public private(set) var radioHasBond: Bool
    public private(set) var phoneHasBond: Bool
    /// What the user does at the iOS pairing prompt.
    public var acceptsPairing = true
    /// Calls to write, for tests that check what the app sent.
    public private(set) var writes: [(Chr, [UInt8])] = []

    public init(radio: RadioState = RadioState(document: .bundled), window: Window = .pairing,
                bonded: Bool = false) {
        self.radio = radio
        self.window = window
        radioHasBond = bonded
        phoneHasBond = bonded
        radio.send = { [weak self] name, bytes in
            MainActor.assumeIsolated {
                guard let self, self.state == .connected, let chr = Chr(rawValue: name) else { return }
                self.onEvent(.value(chr, bytes))
            }
        }
    }

    // MARK: - The radio's buttons

    /// Factory reset or a third phone: the radio drops this phone's bond.
    public func radioForgetsPhone() {
        radioHasBond = false
    }

    /// The user forgets the radio in iOS Settings.
    public func phoneForgetsRadio() {
        phoneHasBond = false
    }

    /// The radio drops the link (out of range, power off).
    public func dropLink() {
        guard state == .connected || state == .connecting else { return }
        radio.disconnected()
        state = .idle
        onEvent(.failed(.disconnected))
    }

    // MARK: - RadioLink

    public func startSearching() {
        guard state == .idle else { return }
        state = .searching
        attempt()
    }

    public func stopSearching() {
        if state == .searching {
            state = .idle
        }
    }

    public func disconnect() {
        guard state != .idle else { return }
        if state == .connected {
            radio.disconnected()
        }
        state = .idle
    }

    public func read(_ chr: Chr) async throws -> [UInt8] {
        guard state == .connected else { throw RadioError.notConnected }
        switch radio.read(chr.rawValue) {
        case .success(let bytes): return bytes
        case .failure(let e): throw RadioError(e)
        }
    }

    public func write(_ chr: Chr, _ value: [UInt8]) async throws {
        guard state == .connected else { throw RadioError.notConnected }
        writes.append((chr, value))
        if let e = radio.write(chr.rawValue, value) {
            throw RadioError(e)
        }
    }

    // MARK: - Connection policy

    private func attempt() {
        guard state == .searching else { return }
        switch window {
        case .none:
            return  // not advertising
        case .connect:
            guard radioHasBond else { return }  // filter accept list: not this phone
        case .pairing:
            break
        }
        state = .connecting
        if phoneHasBond && !radioHasBond {
            // iOS: "Peer removed pairing information".
            state = .idle
            onEvent(.failed(.staleBond))
            return
        }
        if !phoneHasBond {
            guard acceptsPairing else {
                state = .idle
                onEvent(.failed(.pairingFailed))
                return
            }
            radioHasBond = true
            phoneHasBond = true
            window = .none  // the radio closes its window once a phone bonds
        }
        state = .connected
    }
}
