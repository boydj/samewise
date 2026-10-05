import Foundation

/// The mock radio: what each characteristic holds and how it answers, as in
/// the firmware's services/ble/ble_service.c. Validation is all-or-nothing:
/// a rejected write changes nothing. Notifications and indications go out
/// through `send`.
///
/// Differences from the radio: no pairing windows or passkey (macOS pairs
/// on its own terms), and confirmations come from the keyboard.
public final class RadioState {
    public static let logSize = 16
    public static let outcomeAlerted: UInt8 = 0
    public static let outcomeTest: UInt8 = 4

    public let document: GattDocument

    /// Called with a characteristic name and the bytes to notify or indicate.
    public var send: (String, [UInt8]) -> Void = { _, _ in }

    public private(set) var home: [String] = []
    public private(set) var travel: [String] = []
    public private(set) var mode: UInt8 = 0
    public private(set) var channel: UInt8 = 0
    public private(set) var filterPreset: UInt8 = 1  // warnings and watches
    public private(set) var filterBitmap = [UInt8](repeating: 0, count: Codec.filterBytes)
    public private(set) var eventVersion: UInt16
    public private(set) var events: [Codec.Event]
    public private(set) var utc: Int64?
    public private(set) var tz = "UTC0"
    public private(set) var presets: [Codec.Preset] = []
    public private(set) var status = Codec.Status()
    public private(set) var log: [Codec.LogEntry] = []  // newest first
    public private(set) var factoryResetPending = false

    private var eventIndex: UInt8 = 0
    private var logIndex: UInt8 = 0
    private var staging: [Codec.Event]?
    private var stagingVersion: UInt16 = 0
    private var stagingCount = 0
    private var lastNotifiedStatus: Codec.Status?

    public init(document: GattDocument) {
        self.document = document
        eventVersion = UInt16(document.defaultEventTable.version)
        events = document.defaultEventTable.entries.map {
            Codec.Event(code: $0.code, eventClass: UInt8($0.eventClass), name: $0.name)
        }
    }

    /// Every characteristic this mock answers for.
    public static let served: Set<String> = [
        "counties", "travel_counties", "mode", "event_filter", "event_table", "time", "presets",
        "status", "alert_log", "control",
    ]

    // MARK: - Reads

    public func read(_ name: String) -> Result<[UInt8], AttError> {
        guard let c = document.characteristic(name), c.readable, RadioState.served.contains(name)
        else { return .failure(.readNotPermitted) }
        switch name {
        case "counties":
            return .success(Codec.encodeCounties(home))
        case "travel_counties":
            return .success(Codec.encodeCounties(travel))
        case "mode":
            return .success(Codec.encodeMode(mode: mode, channel: channel))
        case "event_filter":
            return .success(Codec.encodeFilter(preset: filterPreset, bitmap: filterBitmap))
        case "event_table":
            guard Int(eventIndex) < events.count else { return .failure(.index) }
            return .success(Codec.encodeEventTableRead(version: eventVersion, count: UInt8(events.count),
                                                       index: eventIndex, event: events[Int(eventIndex)]))
        case "presets":
            return .success(Codec.encodePresets(presets))
        case "status":
            return .success(Codec.encodeStatus(status))
        case "alert_log":
            guard Int(logIndex) < log.count else { return .failure(.index) }
            return .success(Codec.encodeLogEntry(count: UInt8(log.count), index: logIndex, log[Int(logIndex)]))
        default:
            return .failure(.readNotPermitted)
        }
    }

    // MARK: - Writes

    /// Returns nil when accepted, or the ATT error.
    public func write(_ name: String, _ b: [UInt8]) -> AttError? {
        guard let c = document.characteristic(name), c.writable, RadioState.served.contains(name)
        else { return .writeNotPermitted }
        do {
            switch name {
            case "counties":
                home = try Codec.decodeCounties(b)
            case "travel_counties":
                travel = try Codec.decodeCounties(b)
            case "mode":
                let m = try Codec.decodeMode(b)
                mode = m.mode
                channel = m.channel
                status.channel = m.channel == 0 ? 7 : m.channel
            case "event_filter":
                let f = try Codec.decodeFilter(b)
                filterPreset = f.preset
                filterBitmap = f.bitmap
            case "event_table":
                return try writeEventTable(Codec.decodeEventTableWrite(b))
            case "time":
                let t = try Codec.decodeTime(b)
                utc = t.utc
                tz = t.tz
            case "presets":
                presets = try Codec.decodePresets(b)
            case "alert_log":
                logIndex = try Codec.decodeLogSelect(b)
                return nil
            case "control":
                return try control(Codec.decodeCommand(b))
            default:
                return .writeNotPermitted
            }
        } catch let e as AttError {
            return e
        } catch {
            return .value
        }
        notifyStatus(force: true)  // every accepted settings write
        return nil
    }

    private func writeEventTable(_ w: Codec.EventTableWrite) -> AttError? {
        switch w {
        case .select(let index):
            guard Int(index) < events.count else { return .index }
            eventIndex = index
        case .begin(let version, let count):
            guard count > 0 else { return .value }  // an empty table would silence every alert
            staging = []
            stagingVersion = version
            stagingCount = Int(count)
        case .entry(let index, let event):
            guard var s = staging, Int(index) == s.count, s.count < stagingCount else { return .sequence }
            guard !s.contains(where: { $0.code == event.code }) else { return .value }
            s.append(event)
            staging = s
        case .commit:
            guard let s = staging, s.count == stagingCount else { return .sequence }
            events = s
            eventVersion = stagingVersion
            eventIndex = 0
            staging = nil
            notifyStatus(force: true)
        case .abort:
            staging = nil
        }
        return nil
    }

    // MARK: - Control

    private func control(_ cmd: Codec.Command) -> AttError? {
        guard !factoryResetPending else { return .busy }
        switch cmd {
        case .testAlert:
            appendLog(raw: "TEST ALERT", outcome: RadioState.outcomeTest)
            send("control", Codec.encodeControlIndication(cmd, .done))
        case .clearLog:
            log.removeAll()
            send("control", Codec.encodeControlIndication(cmd, .done))
        case .factoryReset:
            factoryResetPending = true
            send("control", Codec.encodeControlIndication(cmd, .awaitingConfirmation))
        }
        return nil
    }

    /// Long-press STBY on the radio.
    public func confirm() {
        guard factoryResetPending else { return }
        factoryResetPending = false
        home = []
        travel = []
        mode = 0
        channel = 0
        filterPreset = 1
        filterBitmap = [UInt8](repeating: 0, count: Codec.filterBytes)
        eventVersion = UInt16(document.defaultEventTable.version)
        events = document.defaultEventTable.entries.map {
            Codec.Event(code: $0.code, eventClass: UInt8($0.eventClass), name: $0.name)
        }
        tz = "UTC0"
        presets = []
        log.removeAll()
        send("control", Codec.encodeControlIndication(.factoryReset, .done))
    }

    /// Any other key on the radio, or 30 seconds.
    public func cancel() {
        guard factoryResetPending else { return }
        factoryResetPending = false
        send("control", Codec.encodeControlIndication(.factoryReset, .cancelled))
    }

    // MARK: - Simulated radio events

    /// A matching alert as the radio would log it: SAME header, outcome alerted.
    public func injectAlert(event: String = "TOR", location: String = "048453", now: Date = Date()) {
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(identifier: "UTC")!
        let day = cal.ordinality(of: .day, in: .year, for: now) ?? 1
        let hh = cal.component(.hour, from: now)
        let mm = cal.component(.minute, from: now)
        let issue = String(format: "%03d%02d%02d", day, hh, mm)
        appendLog(raw: "ZCZC-WXR-\(event)-\(location)+0030-\(issue)-KEWX/NWS-", outcome: RadioState.outcomeAlerted)
    }

    public func setBattery(_ percent: UInt8) {
        status.batteryPercent = min(percent, 100)
        notifyStatus(force: false)
    }

    public func setSignal(snr: Int8, rssi: Int8) {
        status.snrDb = snr
        status.rssiDbuv = rssi
        notifyStatus(force: false)
    }

    private func appendLog(raw: String, outcome: UInt8) {
        let entry = Codec.LogEntry(receivedUtc: utc ?? -1, outcome: outcome, flags: 0, raw: raw)
        log.insert(entry, at: 0)
        if log.count > RadioState.logSize {
            log.removeLast()
        }
        send("alert_log", Codec.encodeLogEntry(count: UInt8(log.count), index: 0, entry))
    }

    /// The radio's rule: any change but the signal, or the signal moving 3 dB.
    private func notifyStatus(force: Bool) {
        if !force, let last = lastNotifiedStatus,
           last.batteryPercent == status.batteryPercent, last.healthFlags == status.healthFlags,
           last.locked == status.locked, last.channel == status.channel,
           abs(Int(status.snrDb) - Int(last.snrDb)) < 3, abs(Int(status.rssiDbuv) - Int(last.rssiDbuv)) < 3 {
            return
        }
        lastNotifiedStatus = status
        send("status", Codec.encodeStatus(status))
    }

    /// The phone disconnected: indexed selections and a staged table are dropped.
    public func disconnected() {
        eventIndex = 0
        logIndex = 0
        staging = nil
        if factoryResetPending {
            cancel()
        }
    }
}
