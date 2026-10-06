// The radio's Bluetooth settings service on a Mac, for developing the
// iPhone app before hardware exists. See README.md.

import Foundation
import GattModel

#if canImport(CoreBluetooth)
import CoreBluetooth

final class Peripheral: NSObject, CBPeripheralManagerDelegate {
    let document: GattDocument
    let radio: RadioState
    var manager: CBPeripheralManager!
    var characteristics: [String: CBMutableCharacteristic] = [:]
    var names: [CBUUID: String] = [:]
    var backlog: [(CBMutableCharacteristic, Data)] = []

    init(document: GattDocument, radio: RadioState) {
        self.document = document
        self.radio = radio
        super.init()
        radio.send = { [weak self] name, bytes in self?.update(name, bytes) }
        manager = CBPeripheralManager(delegate: self, queue: nil)
    }

    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            print("Bluetooth is not on (state \(peripheral.state.rawValue))")
            return
        }
        let service = CBMutableService(type: CBUUID(string: document.service.uuid), primary: true)
        var list: [CBMutableCharacteristic] = []
        for c in document.characteristics {
            // Encryption required everywhere, so the iPhone pairs as with the radio.
            var properties: CBCharacteristicProperties = []
            var permissions: CBAttributePermissions = []
            if c.readable {
                properties.insert(.read)
                permissions.insert(.readEncryptionRequired)
            }
            if c.writable {
                properties.insert(.write)
                permissions.insert(.writeEncryptionRequired)
            }
            if c.notifies {
                properties.insert(.notifyEncryptionRequired)
            }
            if c.indicates {
                properties.insert(.indicateEncryptionRequired)
            }
            let ch = CBMutableCharacteristic(type: CBUUID(string: c.uuid), properties: properties,
                                             value: nil, permissions: permissions)
            characteristics[c.name] = ch
            names[ch.uuid] = c.name
            list.append(ch)
        }
        service.characteristics = list
        peripheral.add(service)
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        if let error = error {
            print("Adding the service failed: \(error)")
            return
        }
        peripheral.startAdvertising([
            CBAdvertisementDataLocalNameKey: "WX Radio mock",
            CBAdvertisementDataServiceUUIDsKey: [service.uuid],
        ])
        print("Advertising \(document.service.uuid) with \(document.characteristics.count) characteristics")
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        guard let name = names[request.characteristic.uuid] else {
            peripheral.respond(to: request, withResult: .attributeNotFound)
            return
        }
        switch radio.read(name) {
        case .success(let bytes):
            guard request.offset <= bytes.count else {
                peripheral.respond(to: request, withResult: .invalidOffset)
                return
            }
            request.value = Data(bytes[request.offset...])
            peripheral.respond(to: request, withResult: .success)
        case .failure(let error):
            peripheral.respond(to: request, withResult: attResult(error))
        }
    }

    /// Long writes arrive as several requests: join them per characteristic,
    /// then apply each whole value. Respond once, to the first request.
    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        var values: [(String, [UInt8])] = []
        for r in requests {
            guard let name = names[r.characteristic.uuid] else {
                peripheral.respond(to: requests[0], withResult: .attributeNotFound)
                return
            }
            let chunk = [UInt8](r.value ?? Data())
            if let i = values.firstIndex(where: { $0.0 == name }) {
                guard r.offset == values[i].1.count else {
                    peripheral.respond(to: requests[0], withResult: .invalidOffset)
                    return
                }
                values[i].1 += chunk
            } else {
                guard r.offset == 0 else {
                    peripheral.respond(to: requests[0], withResult: .invalidOffset)
                    return
                }
                values.append((name, chunk))
            }
        }
        for (name, bytes) in values {
            if let error = radio.write(name, bytes) {
                print("Rejected write to \(name): ATT error 0x\(String(error.code, radix: 16))")
                peripheral.respond(to: requests[0], withResult: attResult(error))
                return
            }
            print("Wrote \(name) (\(bytes.count) bytes)")
        }
        peripheral.respond(to: requests[0], withResult: .success)
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral,
                           didSubscribeTo characteristic: CBCharacteristic) {
        print("Subscribed: \(names[characteristic.uuid] ?? "?")")
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral,
                           didUnsubscribeFrom characteristic: CBCharacteristic) {
        print("Unsubscribed: \(names[characteristic.uuid] ?? "?")")
        if names[characteristic.uuid] == "control" {
            radio.disconnected()
        }
    }

    func peripheralManagerIsReady(toUpdateSubscribers peripheral: CBPeripheralManager) {
        while let item = backlog.first {
            guard peripheral.updateValue(item.1, for: item.0, onSubscribedCentrals: nil) else { return }
            backlog.removeFirst()
        }
    }

    func update(_ name: String, _ bytes: [UInt8]) {
        guard let ch = characteristics[name] else { return }
        let data = Data(bytes)
        if !backlog.isEmpty || !manager.updateValue(data, for: ch, onSubscribedCentrals: nil) {
            backlog.append((ch, data))
        }
    }

    func attResult(_ e: AttError) -> CBATTError.Code {
        CBATTError.Code(rawValue: Int(e.code)) ?? .unlikelyError
    }
}

let help = """
    Commands:
      a [EEE] [PSSCCC]   a matching alert (default TOR 048453): log entry and notification
      t                  a test alert, as from the Control characteristic
      b <percent>        battery level
      s <snr> [rssi]     signal quality in dB (RSSI in dBuV, default 40)
      y                  long-press STBY: confirm a pending factory reset
      n                  any other key: cancel it
      p                  print the settings
      q                  quit
    """

func printState(_ r: RadioState) {
    print("counties \(r.home) travel \(r.travel) mode \(r.mode) channel \(r.channel)")
    print("filter \(r.filterPreset) events v\(r.eventVersion) (\(r.events.count)) tz \(r.tz) utc \(r.utc.map { String($0) } ?? "unset")")
    print("presets \(r.presets.map { "\($0.band):\($0.khz)" }) status \(r.status) log \(r.log.count)")
}

func handle(_ line: String, _ radio: RadioState) {
    let words = line.split(separator: " ").map(String.init)
    guard let command = words.first else { return }
    switch command {
    case "a":
        radio.injectAlert(event: words.count > 1 ? words[1] : "TOR",
                          location: words.count > 2 ? words[2] : "048453")
    case "t":
        _ = radio.write("control", Codec.encodeCommand(.testAlert))
    case "b":
        radio.setBattery(UInt8(words.count > 1 ? words[1] : "") ?? 100)
    case "s":
        radio.setSignal(snr: Int8(words.count > 1 ? words[1] : "") ?? 25,
                        rssi: Int8(words.count > 2 ? words[2] : "") ?? 40)
    case "y":
        radio.confirm()
    case "n":
        radio.cancel()
    case "p":
        printState(radio)
    case "q":
        exit(0)
    default:
        print(help)
    }
}

let path = CommandLine.arguments.count > 1 ? URL(fileURLWithPath: CommandLine.arguments[1])
                                           : GattDocument.bundledURL
let document: GattDocument
do {
    document = try GattDocument.load(from: path)
} catch {
    print("Can't read \(path.path): \(error)")
    exit(1)
}
let radio = RadioState(document: document)
let peripheral = Peripheral(document: document, radio: radio)
print(help)
DispatchQueue.global().async {
    while let line = readLine() {
        DispatchQueue.main.async { handle(line, radio) }
    }
    exit(0)
}
dispatchMain()

#else

print("The mock peripheral needs CoreBluetooth (macOS). `swift test` runs the model anywhere.")
exit(1)

#endif
