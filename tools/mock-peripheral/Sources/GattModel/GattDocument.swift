import Foundation

/// docs/gatt.json, generated from the firmware's services/ble/gatt_table.h.
public struct GattDocument: Decodable {
    public struct Service: Decodable {
        public let name: String
        public let uuid: String
    }

    public struct Characteristic: Decodable {
        public let name: String
        public let uuid: String
        public let properties: [String]
        public let maxLength: Int
        public let schema: Int
        public let layout: String

        enum CodingKeys: String, CodingKey {
            case name, uuid, properties, schema, layout
            case maxLength = "max_length"
        }

        public var readable: Bool { properties.contains("read") }
        public var writable: Bool { properties.contains("write") }
        public var notifies: Bool { properties.contains("notify") }
        public var indicates: Bool { properties.contains("indicate") }
    }

    public struct Event: Decodable {
        public let code: String
        public let eventClass: Int
        public let name: String

        enum CodingKeys: String, CodingKey {
            case code, name
            case eventClass = "class"
        }
    }

    public struct EventTable: Decodable {
        public let version: Int
        public let entries: [Event]
    }

    public struct ErrorCode: Decodable {
        public let name: String
        public let code: Int
        public let meaning: String
    }

    public let service: Service
    public let characteristics: [Characteristic]
    public let defaultEventTable: EventTable
    public let errors: [ErrorCode]

    enum CodingKeys: String, CodingKey {
        case service, characteristics, errors
        case defaultEventTable = "default_event_table"
    }

    public static func load(from url: URL) throws -> GattDocument {
        try JSONDecoder().decode(GattDocument.self, from: Data(contentsOf: url))
    }

    /// docs/gatt.json in this repository, found from this source file's path.
    public static var repositoryURL: URL {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()  // GattModel
            .deletingLastPathComponent()  // Sources
            .deletingLastPathComponent()  // mock-peripheral
            .deletingLastPathComponent()  // tools
            .deletingLastPathComponent()  // repository
            .appendingPathComponent("docs/gatt.json")
    }

    public func characteristic(_ name: String) -> Characteristic? {
        characteristics.first { $0.name == name }
    }
}
