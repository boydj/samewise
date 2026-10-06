// swift-tools-version:5.9
//
// Everything in the iPhone app that isn't a view: the radio link, the
// session behind every screen, time zone rules, counties and error text.
// Foundation and Observation only (CoreBluetooth and CoreLocation behind
// canImport), so `swift test` runs it on macOS and Linux.

import PackageDescription

let package = Package(
    name: "SamewiseKit",
    platforms: [.macOS(.v14), .iOS(.v17)],
    products: [
        .library(name: "SamewiseKit", targets: ["SamewiseKit"]),
    ],
    dependencies: [
        .package(path: "../GattModel"),
    ],
    targets: [
        .target(name: "SamewiseKit", dependencies: ["GattModel"]),
        .testTarget(name: "SamewiseKitTests", dependencies: ["SamewiseKit"]),
    ]
)
