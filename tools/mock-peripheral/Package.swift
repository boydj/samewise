// swift-tools-version:5.9
//
// macOS mock of the radio's Bluetooth settings service, built from
// docs/gatt.json, so the iPhone app can be developed before hardware exists.

import PackageDescription

let package = Package(
    name: "MockPeripheral",
    platforms: [.macOS(.v13)],
    targets: [
        // The radio's behaviour: values, validation, indexed reads, Control.
        // Foundation only, so it is unit-tested without Bluetooth.
        .target(name: "GattModel"),
        // CoreBluetooth peripheral and keyboard commands.
        .executableTarget(name: "MockPeripheral", dependencies: ["GattModel"]),
        .testTarget(name: "GattModelTests", dependencies: ["GattModel"]),
    ]
)
