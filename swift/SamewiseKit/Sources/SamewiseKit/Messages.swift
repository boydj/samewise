import GattModel

/// What the connect screen tells you to do.
public enum ConnectAdvice: Equatable, Sendable {
    /// Not searching yet.
    case start(firstTime: Bool)
    /// Searching: the radio advertises only while a window is open.
    case openWindow(firstTime: Bool)
    case connecting
    case staleBond
    case pairingFailed
    case bluetooth(BluetoothProblem)
    case lostConnection

    public static func make(state: LinkState, problem: RadioError?, pairedBefore: Bool) -> ConnectAdvice {
        switch problem {
        case .staleBond?: return .staleBond
        case .pairingFailed?: return .pairingFailed
        case .bluetooth(let p)?: return .bluetooth(p)
        case .disconnected? where state == .idle: return .lostConnection
        default: break
        }
        switch state {
        case .idle: return .start(firstTime: !pairedBefore)
        case .searching: return .openWindow(firstTime: !pairedBefore)
        case .connecting, .connected: return .connecting
        }
    }

    public var title: String {
        switch self {
        case .start(let first): return first ? "Pair with your radio" : "Connect to your radio"
        case .openWindow: return "Looking for your radio"
        case .connecting: return "Connecting"
        case .staleBond: return "Pair this iPhone again"
        case .pairingFailed: return "Pairing didn't finish"
        case .bluetooth(.off): return "Bluetooth is off"
        case .bluetooth(.unauthorized): return "Bluetooth permission needed"
        case .bluetooth(.unsupported): return "No Bluetooth"
        case .lostConnection: return "Connection lost"
        }
    }

    /// Steps, in order.
    public var steps: [String] {
        let pairing = "On the radio, press BAND and STBY together. It shows a 6-digit passkey for 60 seconds."
        let reconnect = "On the radio, hold BAND. It looks for your iPhone for 2 minutes."
        switch self {
        case .start(let first), .openWindow(let first):
            return first
                ? [pairing, "When iPhone asks, type the passkey from the radio's screen."]
                : [reconnect]
        case .connecting:
            return ["If iPhone asks for a passkey, type the one on the radio's screen."]
        case .staleBond:
            return [
                "The radio no longer knows this iPhone: it was reset, or another phone took its place.",
                "On this iPhone, open Settings, then Bluetooth.",
                "Tap the info button next to the radio, then Forget This Device.",
                "Come back here and tap Pair. " + pairing,
            ]
        case .pairingFailed:
            return ["The passkey may not have matched, or the request was cancelled.", pairing,
                    "Tap Pair, then type the passkey when iPhone asks."]
        case .bluetooth(.off):
            return ["Turn on Bluetooth in Control Center or Settings."]
        case .bluetooth(.unauthorized):
            return ["Open Settings, then WX Radio, and allow Bluetooth."]
        case .bluetooth(.unsupported):
            return ["This device can't use Bluetooth Low Energy."]
        case .lostConnection:
            return ["The radio may be out of range or switched off.", reconnect]
        }
    }

    /// The main button's label, or nil while there is nothing to press.
    public var action: String? {
        switch self {
        case .start(let first): return first ? "Pair" : "Connect"
        case .staleBond, .pairingFailed: return "Pair"
        case .lostConnection: return "Reconnect"
        case .bluetooth(.unsupported), .openWindow, .connecting: return nil
        case .bluetooth: return "Try again"
        }
    }
}

extension RadioError {
    /// Plain-language text for the screen. ATT errors from the radio use the
    /// names in docs/gatt.json.
    public var message: String {
        switch self {
        case .att(let code):
            return Self.attMessage(code)
        case .bluetooth(let p):
            return ConnectAdvice.bluetooth(p).title + "."
        case .staleBond:
            return "The radio no longer knows this iPhone. Forget the radio in Settings, then pair again."
        case .pairingFailed:
            return "Pairing didn't finish. Check the passkey on the radio's screen and try again."
        case .notConnected:
            return "Not connected to the radio."
        case .disconnected:
            return "The connection to the radio was lost. Nothing more was changed."
        case .timeout:
            return "The radio didn't answer. Move closer and try again."
        }
    }

    static func attMessage(_ code: UInt8) -> String {
        switch AttError.allCases.first(where: { $0.code == code }) {
        case .length?, .schema?:
            return "The radio didn't understand that. Its firmware may be newer than this app; update the app."
        case .value?:
            return "The radio refused that value. Nothing was changed."
        case .storage?:
            return "The radio applied the change but couldn't save it. It will be lost when the radio restarts."
        case .sequence?:
            return "The event table update was interrupted. Nothing was changed; try again."
        case .busy?:
            return "The radio is waiting for you to confirm a factory reset. Long-press STBY to confirm, or press any other key to cancel."
        case .index?:
            return "That entry no longer exists on the radio."
        case .readNotPermitted?, .writeNotPermitted?:
            return "The radio doesn't allow that."
        case nil:
            switch code {
            case 0x05, 0x0F:
                return "The radio needs this iPhone to be paired first."
            default:
                return "The radio refused the request (error \(code))."
            }
        }
    }
}
