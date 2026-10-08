#if canImport(CoreBluetooth)
import CoreBluetooth
import Foundation
import GattModel
import os

/// The radio over CoreBluetooth. Scans for the settings service (the radio
/// advertises only while a connect or pairing window is open), connects to
/// the first radio found, discovers the characteristics and subscribes to
/// Status, Alert log and Control. Every characteristic needs an encrypted,
/// authenticated link, so the first subscription makes iOS pair: it shows
/// its own prompt for the passkey on the radio's screen.
///
/// Reads and writes complete in order per characteristic. A notification
/// that arrives while a read of the same characteristic is pending is taken
/// as the read's answer; only the Alert log can do that, and the session
/// re-reads the log on every connection.
@MainActor
public final class CoreBluetoothLink: NSObject, RadioLink {
    public private(set) var state: LinkState = .idle {
        didSet {
            if state != oldValue {
                onEvent(.state(state))
            }
        }
    }
    public var onEvent: (LinkEvent) -> Void = { _ in }

    /// What iOS reported at each step, for Xcode's console and Console.app
    /// (subsystem samewise, category bluetooth).
    private static let log = Logger(subsystem: "samewise", category: "bluetooth")

    private let serviceUUID: CBUUID
    private let uuids: [CBUUID: Chr]
    private var central: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var characteristics: [Chr: CBCharacteristic] = [:]
    private var awaitingSubscription: Set<Chr> = []
    private var reads: [Chr: [CheckedContinuation<[UInt8], Error>]] = [:]
    private var writes: [Chr: [CheckedContinuation<Void, Error>]] = [:]

    public init(document: GattDocument = .bundled) {
        serviceUUID = CBUUID(string: document.service.uuid)
        var uuids: [CBUUID: Chr] = [:]
        for c in document.characteristics {
            if let chr = Chr(rawValue: c.name) {
                uuids[CBUUID(string: c.uuid)] = chr
            }
        }
        self.uuids = uuids
        super.init()
    }

    // MARK: - RadioLink

    public func startSearching() {
        guard state == .idle else { return }
        state = .searching
        if let central {
            scanIfReady(central)
        } else {
            // Created on first use: this is what asks for Bluetooth permission.
            central = CBCentralManager(delegate: self, queue: nil)
        }
    }

    public func stopSearching() {
        guard state == .searching else { return }
        central?.stopScan()
        state = .idle
    }

    public func disconnect() {
        central?.stopScan()
        if let peripheral {
            central?.cancelPeripheralConnection(peripheral)
        }
        reset(failing: .disconnected)
    }

    public func read(_ chr: Chr) async throws -> [UInt8] {
        guard state == .connected, let peripheral, let c = characteristics[chr] else {
            throw RadioError.notConnected
        }
        return try await withCheckedThrowingContinuation { cont in
            reads[chr, default: []].append(cont)
            peripheral.readValue(for: c)
        }
    }

    public func write(_ chr: Chr, _ value: [UInt8]) async throws {
        guard state == .connected, let peripheral, let c = characteristics[chr] else {
            throw RadioError.notConnected
        }
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            writes[chr, default: []].append(cont)
            peripheral.writeValue(Data(value), for: c, type: .withResponse)
        }
    }

    // MARK: - State

    private func scanIfReady(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            if state == .searching {
                central.scanForPeripherals(withServices: [serviceUUID])
            }
        case .poweredOff:
            fail(.bluetooth(.off))
        case .unauthorized:
            fail(.bluetooth(.unauthorized))
        case .unsupported:
            fail(.bluetooth(.unsupported))
        default:
            break  // unknown or resetting: wait for the next update
        }
    }

    private func fail(_ error: RadioError) {
        Self.log.error("Link failed: \(String(describing: error), privacy: .public)")
        central?.stopScan()
        if let peripheral {
            central?.cancelPeripheralConnection(peripheral)
        }
        reset(failing: error)
        onEvent(.failed(error))
    }

    private func reset(failing error: RadioError) {
        peripheral?.delegate = nil
        peripheral = nil
        characteristics = [:]
        awaitingSubscription = []
        let pendingReads = reads.values.flatMap { $0 }
        let pendingWrites = writes.values.flatMap { $0 }
        reads = [:]
        writes = [:]
        pendingReads.forEach { $0.resume(throwing: error) }
        pendingWrites.forEach { $0.resume(throwing: error) }
        state = .idle
    }

    /// What iOS reported, as the app explains it.
    static func radioError(_ error: Error) -> RadioError {
        let ns = error as NSError
        log.error("iOS error \(ns.domain, privacy: .public) \(ns.code): \(ns.localizedDescription, privacy: .public)")
        if let e = error as? CBATTError {
            switch e.code {
            case .insufficientAuthentication, .insufficientEncryption, .insufficientEncryptionKeySize:
                return .pairingFailed
            default:
                return .att(UInt8(truncatingIfNeeded: e.code.rawValue))
            }
        }
        if let e = error as? CBError {
            switch e.code {
            case .peerRemovedPairingInformation:
                return .staleBond
            case .encryptionTimedOut:
                return .pairingFailed
            case .connectionTimeout:
                return .timeout
            default:
                return .disconnected
            }
        }
        if ns.domain == CBATTErrorDomain {
            return .att(UInt8(truncatingIfNeeded: ns.code))
        }
        return .disconnected
    }

    // MARK: - Delegate callbacks, on the main queue

    private func discovered(_ p: CBPeripheral) {
        guard state == .searching, let central else { return }
        Self.log.info("Found \(p.name ?? "unnamed", privacy: .public); connecting")
        central.stopScan()
        peripheral = p
        p.delegate = self
        state = .connecting
        central.connect(p)
    }

    private func discoveredCharacteristics(_ list: [CBCharacteristic]) {
        for c in list {
            if let chr = uuids[c.uuid] {
                characteristics[chr] = c
            }
        }
        guard Set(characteristics.keys) == Set(Chr.allCases) else {
            fail(.att(AttError.readNotPermitted.code))  // not the radio's service as we know it
            return
        }
        Self.log.info("Found the service; subscribing, which makes iOS pair")
        awaitingSubscription = Set(Chr.subscribed)
        for chr in Chr.subscribed {
            peripheral?.setNotifyValue(true, for: characteristics[chr]!)
        }
    }

    private func subscribed(_ chr: Chr, error: Error?) {
        if let error {
            Self.log.error("Subscribing to \(chr.rawValue, privacy: .public) failed")
            fail(Self.radioError(error))
            return
        }
        Self.log.info("Subscribed to \(chr.rawValue, privacy: .public)")
        awaitingSubscription.remove(chr)
        if awaitingSubscription.isEmpty && state == .connecting {
            state = .connected
        }
    }

    private func valueArrived(_ chr: Chr, _ data: Data?, error: Error?) {
        if var queue = reads[chr], !queue.isEmpty {
            let cont = queue.removeFirst()
            reads[chr] = queue
            if let error {
                cont.resume(throwing: Self.radioError(error))
            } else {
                cont.resume(returning: [UInt8](data ?? Data()))
            }
        } else if error == nil, let data, state == .connected {
            onEvent(.value(chr, [UInt8](data)))
        }
    }

    private func wrote(_ chr: Chr, error: Error?) {
        guard var queue = writes[chr], !queue.isEmpty else { return }
        let cont = queue.removeFirst()
        writes[chr] = queue
        if let error {
            cont.resume(throwing: Self.radioError(error))
        } else {
            cont.resume()
        }
    }
}

extension CoreBluetoothLink: CBCentralManagerDelegate {
    nonisolated public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        MainActor.assumeIsolated { scanIfReady(central) }
    }

    nonisolated public func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                           advertisementData: [String: Any], rssi RSSI: NSNumber) {
        MainActor.assumeIsolated { discovered(peripheral) }
    }

    nonisolated public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        MainActor.assumeIsolated {
            peripheral.discoverServices([serviceUUID])
        }
    }

    nonisolated public func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                                           error: Error?) {
        MainActor.assumeIsolated { fail(error.map(Self.radioError) ?? .disconnected) }
    }

    nonisolated public func centralManager(_ central: CBCentralManager,
                                           didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        MainActor.assumeIsolated {
            guard state != .idle else { return }  // we asked for it
            fail(error.map(Self.radioError) ?? .disconnected)
        }
    }
}

extension CoreBluetoothLink: CBPeripheralDelegate {
    nonisolated public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        MainActor.assumeIsolated {
            if let error {
                fail(Self.radioError(error))
                return
            }
            guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
                fail(.disconnected)
                return
            }
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    nonisolated public func peripheral(_ peripheral: CBPeripheral,
                                       didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        MainActor.assumeIsolated {
            if let error {
                fail(Self.radioError(error))
                return
            }
            discoveredCharacteristics(service.characteristics ?? [])
        }
    }

    nonisolated public func peripheral(_ peripheral: CBPeripheral,
                                       didUpdateNotificationStateFor characteristic: CBCharacteristic,
                                       error: Error?) {
        MainActor.assumeIsolated {
            if let chr = uuids[characteristic.uuid] {
                subscribed(chr, error: error)
            }
        }
    }

    nonisolated public func peripheral(_ peripheral: CBPeripheral,
                                       didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        MainActor.assumeIsolated {
            if let chr = uuids[characteristic.uuid] {
                valueArrived(chr, characteristic.value, error: error)
            }
        }
    }

    nonisolated public func peripheral(_ peripheral: CBPeripheral,
                                       didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        MainActor.assumeIsolated {
            if let chr = uuids[characteristic.uuid] {
                wrote(chr, error: error)
            }
        }
    }
}
#endif
