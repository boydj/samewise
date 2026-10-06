import Foundation
import GattModel
import Observation

/// The radio as the app's screens see it: the link's state, the radio's
/// settings, status, event table and alert log, and the last problem.
///
/// Every value shown is the radio's own: after any write the session reads
/// the characteristic back, so a rejected write leaves the screen showing
/// what the radio actually holds.
@MainActor
@Observable
public final class RadioSession {
    public struct Settings: Equatable, Sendable {
        public var home: [String] = []
        public var travel: [String] = []
        public var travelMode = false
        /// 0 scans for the strongest weather channel; 1-7 is fixed.
        public var channel: UInt8 = 0
        public var filterPreset: UInt8 = 1
        public var filterBitmap = [UInt8](repeating: 0, count: Codec.filterBytes)
        public var presets: [Codec.Preset] = []

        public init() {}
    }

    /// A Control result from the radio's indication.
    public struct ControlOutcome: Equatable, Sendable {
        public var command: Codec.Command
        public var result: Codec.ControlResult
    }

    public let document: GattDocument
    public let link: RadioLink

    public private(set) var linkState: LinkState = .idle
    public private(set) var settings = Settings()
    /// True once everything has been read on this connection.
    public private(set) var loaded = false
    public private(set) var status: Codec.Status?
    public private(set) var eventVersion: UInt16 = 0
    public private(set) var events: [Codec.Event] = []
    /// Newest first, as the radio indexes it.
    public private(set) var log: [Codec.LogEntry] = []
    public private(set) var lastControl: ControlOutcome?
    /// The last thing that went wrong, for the screen to explain; cleared by
    /// the next success or dismiss().
    public private(set) var problem: RadioError?

    /// Run on every connection before the settings are read (time sync,
    /// travel counties). Set by the app.
    @ObservationIgnored
    public var onConnect: [(RadioSession) async -> Void] = []

    @ObservationIgnored
    private var connectTask: Task<Void, Never>?

    public init(link: RadioLink, document: GattDocument = .bundled) {
        self.link = link
        self.document = document
        link.onEvent = { [weak self] event in self?.handle(event) }
        linkState = link.state
    }

    // MARK: - Connection

    public func connect() {
        problem = nil
        link.startSearching()
    }

    public func disconnect() {
        link.disconnect()
    }

    /// Waits for the work started by the last connection to finish.
    public func settled() async {
        await connectTask?.value
    }

    public func dismissProblem() {
        problem = nil
    }

    private func handle(_ event: LinkEvent) {
        switch event {
        case .state(let s):
            linkState = s
            if s == .connected {
                connectTask = Task { await self.didConnect() }
            } else if s == .idle {
                loaded = false
            }
        case .value(let chr, let bytes):
            received(chr, bytes)
        case .failed(let e):
            problem = e
        }
    }

    private func didConnect() async {
        for step in onConnect {
            await step(self)
        }
        await reloadAll()
    }

    // MARK: - Reading

    public func reloadAll() async {
        do {
            for chr in [Chr.counties, .travelCounties, .mode, .eventFilter, .presets, .status] {
                try await reload(chr)
            }
            try await reloadEventTable()
            try await reloadLog()
            loaded = true
        } catch {
            fail(error)
        }
    }

    func reload(_ chr: Chr) async throws {
        let b = try await link.read(chr)
        switch chr {
        case .counties: settings.home = try Codec.decodeCounties(b)
        case .travelCounties: settings.travel = try Codec.decodeCounties(b)
        case .mode:
            let m = try Codec.decodeMode(b)
            settings.travelMode = m.mode == 1
            settings.channel = m.channel
        case .eventFilter:
            let f = try Codec.decodeFilter(b)
            settings.filterPreset = f.preset
            settings.filterBitmap = f.bitmap
        case .presets: settings.presets = try Codec.decodePresets(b)
        case .status: status = try Codec.decodeStatus(b)
        case .eventTable: try await reloadEventTable()
        case .alertLog: try await reloadLog()
        case .time, .control: break  // write-only
        }
    }

    /// The event table, entry by entry (select, then read).
    func reloadEventTable() async throws {
        var entries: [Codec.Event] = []
        var version: UInt16 = 0
        var index: UInt8 = 0
        repeat {
            try await link.write(.eventTable, Codec.encodeEventTableWrite(.select(index: index)))
            let r = try Codec.decodeEventTableRead(await link.read(.eventTable))
            version = r.version
            entries.append(r.event)
            index += 1
            if index >= r.count { break }
        } while true
        events = entries
        eventVersion = version
    }

    /// The alert log, newest first. An empty log refuses the read with INDEX.
    func reloadLog() async throws {
        var entries: [Codec.LogEntry] = []
        var index: UInt8 = 0
        while true {
            try await link.write(.alertLog, Codec.encodeLogSelect(index))
            let b: [UInt8]
            do {
                b = try await link.read(.alertLog)
            } catch RadioError.att(AttError.index.code) where index == 0 {
                break
            }
            let r = try Codec.decodeLogEntry(b)
            entries.append(r.entry)
            index += 1
            if index >= r.count { break }
        }
        log = entries
    }

    // MARK: - Notifications

    private func received(_ chr: Chr, _ b: [UInt8]) {
        do {
            switch chr {
            case .status:
                status = try Codec.decodeStatus(b)
            case .alertLog:
                let r = try Codec.decodeLogEntry(b)
                if r.index == 0 {
                    log.insert(r.entry, at: 0)
                    log = Array(log.prefix(Int(r.count)))
                }
            case .control:
                let (command, result) = try Codec.decodeControlIndication(b)
                lastControl = ControlOutcome(command: command, result: result)
            default:
                break
            }
        } catch {
            fail(error)
        }
    }

    // MARK: - Writing

    /// Write a value, then read the characteristic back so the screen shows
    /// what the radio holds either way. Returns whether the radio accepted it.
    @discardableResult
    public func write(_ chr: Chr, _ value: [UInt8]) async -> Bool {
        var accepted = false
        do {
            try await link.write(chr, value)
            accepted = true
            problem = nil
        } catch {
            fail(error)
        }
        if chr != .time && chr != .control {
            do {
                try await reload(chr)
            } catch {
                if accepted { fail(error) }
            }
        }
        return accepted
    }

    @discardableResult
    public func setHomeCounties(_ codes: [String]) async -> Bool {
        await write(.counties, Codec.encodeCounties(codes))
    }

    @discardableResult
    public func setTravelCounties(_ codes: [String]) async -> Bool {
        await write(.travelCounties, Codec.encodeCounties(codes))
    }

    @discardableResult
    public func setMode(travel: Bool, channel: UInt8) async -> Bool {
        await write(.mode, Codec.encodeMode(mode: travel ? 1 : 0, channel: channel))
    }

    @discardableResult
    public func setFilter(preset: UInt8, bitmap: [UInt8]) async -> Bool {
        await write(.eventFilter, Codec.encodeFilter(preset: preset, bitmap: bitmap))
    }

    @discardableResult
    public func setPresets(_ presets: [Codec.Preset]) async -> Bool {
        await write(.presets, Codec.encodePresets(presets))
    }

    /// Test alert, clear log or factory reset; the result arrives as lastControl.
    @discardableResult
    public func send(_ command: Codec.Command) async -> Bool {
        lastControl = nil
        return await write(.control, Codec.encodeCommand(command))
    }

    private func fail(_ error: Error) {
        if let e = error as? RadioError {
            problem = e
        } else if let e = error as? AttError {
            problem = RadioError(e)  // a value from the radio that doesn't decode
        } else {
            problem = .att(AttError.value.code)
        }
    }
}
