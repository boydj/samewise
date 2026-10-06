// swift-tools-version:5.9
//
// The radio's Bluetooth settings service in Swift: the codec, the POSIX TZ
// parser and the radio's behaviour, ported from services/ble/ and checked
// against docs/gatt.json. Shared by the macOS mock peripheral
// (tools/mock-peripheral) and the iPhone app (ios/Samewise).

import PackageDescription

let package = Package(
    name: "GattModel",
    platforms: [.macOS(.v13), .iOS(.v17)],
    products: [
        .library(name: "GattModel", targets: ["GattModel"]),
    ],
    targets: [
        // Foundation only, so it is unit-tested anywhere, Linux included.
        // Resources/gatt.json is docs/gatt.json, written by tools/gatt/gatt_json.py.
        .target(name: "GattModel", resources: [.copy("Resources/gatt.json")]),
        .testTarget(name: "GattModelTests", dependencies: ["GattModel"]),
    ]
)
