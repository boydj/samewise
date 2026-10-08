// swift-tools-version:5.9
//
// macOS mock of the radio's Bluetooth settings service, built from
// docs/gatt.json, so the iPhone app can be developed before hardware exists.

import PackageDescription

let package = Package(
    name: "MockPeripheral",
    platforms: [.macOS(.v13)],
    dependencies: [
        // The radio's behaviour: values, validation, indexed reads, Control.
        .package(path: "../../swift/GattModel"),
    ],
    targets: [
        // CoreBluetooth peripheral and keyboard commands.
        .executableTarget(name: "MockPeripheral", dependencies: ["GattModel"]),
    ]
)
