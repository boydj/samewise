import Foundation

/// U.S. counties and equivalents (parishes, boroughs, independent cities,
/// territories' municipalities) with their FIPS codes, and their SAME
/// location codes: P (subdivision, 0 = all of it), SS (state FIPS), CCC
/// (county FIPS, 000 = the whole state).
///
/// The data is Resources/counties.txt, written by tools/counties/build_counties.py
/// from the U.S. Census Bureau's FIPS lists; its header names the source and date.
public final class CountyTable: Sendable {
    public struct State: Hashable, Sendable {
        public var fips: String
        public var postal: String
        public var name: String
    }

    public struct County: Hashable, Sendable {
        public var stateFips: String
        public var countyFips: String
        /// As the Census writes it: "Travis County", "Orleans Parish", "San Juan Municipio".
        public var name: String
        public var state: State

        /// The SAME code for the whole county.
        public var code: String { "0" + stateFips + countyFips }
        public var title: String { "\(name), \(state.postal)" }
    }

    /// SAME subdivisions (47 CFR 11.31): 0 is the whole county.
    public static let subdivisions = ["All", "Northwest", "North", "Northeast", "West", "Central", "East",
                                      "Southwest", "South", "Southeast"]

    public let counties: [County]
    public let states: [State]
    /// The header's source line, for the About screen.
    public let source: String
    private let byCode: [String: County]
    private let stateByFips: [String: State]

    public static let shared: CountyTable = {
        guard let url = Bundle.module.url(forResource: "counties", withExtension: "txt"),
              let text = try? String(contentsOf: url, encoding: .utf8) else {
            return CountyTable(text: "")
        }
        return CountyTable(text: text)
    }()

    /// Lines of `SS|CCC|County name|ST|State name`; `#` lines are comments,
    /// and a county code of 000 lists a state on its own.
    public init(text: String) {
        var counties: [County] = []
        var states: [String: State] = [:]
        var source = ""
        for line in text.split(whereSeparator: \.isNewline) {
            if line.hasPrefix("#") {
                if source.isEmpty, line.hasPrefix("# Source:") {
                    source = line.dropFirst("# Source:".count).trimmingCharacters(in: .whitespaces)
                }
                continue
            }
            let f = line.split(separator: "|", omittingEmptySubsequences: false).map(String.init)
            guard f.count == 5, f[0].count == 2, f[1].count == 3 else { continue }
            let state = State(fips: f[0], postal: f[3], name: f[4])
            states[f[0]] = state
            if f[1] != "000" {
                counties.append(County(stateFips: f[0], countyFips: f[1], name: f[2], state: state))
            }
        }
        self.counties = counties
        self.states = states.values.sorted { $0.name < $1.name }
        self.source = source
        stateByFips = states
        byCode = Dictionary(counties.map { ($0.stateFips + $0.countyFips, $0) }, uniquingKeysWith: { a, _ in a })
    }

    public var isEmpty: Bool { counties.isEmpty }

    // MARK: - Names for codes

    /// "Travis County, TX", "Northwest Travis County, TX", "All of Texas",
    /// or the code itself if the table doesn't know it.
    public func name(_ code: String) -> String {
        guard code.count == 6, let p = Int(code.prefix(1)) else { return code }
        let ss = String(code.dropFirst().prefix(2))
        let ccc = String(code.suffix(3))
        if ccc == "000", let state = stateByFips[ss] {
            return p == 0 ? "All of \(state.name)" : "\(Self.subdivisions[p]) \(state.name)"
        }
        guard let county = byCode[ss + ccc] else { return code }
        return p == 0 ? county.title : "\(Self.subdivisions[p]) \(county.title)"
    }

    public func county(code: String) -> County? {
        guard code.count == 6 else { return nil }
        return byCode[String(code.suffix(5))]
    }

    // MARK: - Search

    /// Counties whose name starts with the query's words, optionally
    /// narrowed by a state name or postal code after a comma or at the end:
    /// "travis", "travis tx", "orleans, louisiana", "san juan pr".
    public func search(_ query: String, limit: Int = 50) -> [County] {
        let q = Self.normalize(query)
        guard !q.isEmpty else { return [] }
        var countyPart = q
        var state: State?
        if let comma = query.firstIndex(of: ",") {
            countyPart = Self.normalize(String(query[..<comma]))
            state = findState(Self.normalize(String(query[query.index(after: comma)...])))
        } else {
            // The longest state name that ends the query: "west virginia" over "virginia".
            var best = 0
            for s in states {
                for key in [Self.normalize(s.name), s.postal.lowercased()]
                where key.count > best && q.hasSuffix(" " + key) {
                    state = s
                    best = key.count
                    countyPart = String(q.dropLast(key.count + 1))
                }
            }
        }
        var results = counties.filter { c in
            (state == nil || c.state == state) && Self.normalize(c.name).hasPrefix(countyPart)
        }
        if results.isEmpty, state == nil {
            results = counties.filter { Self.normalize($0.name).contains(countyPart) }
        }
        return Array(results.sorted { ($0.name, $0.state.name) < ($1.name, $1.state.name) }.prefix(limit))
    }

    public func findState(_ text: String) -> State? {
        let t = Self.normalize(text)
        return states.first { Self.normalize($0.name) == t || $0.postal.lowercased() == t }
    }

    // MARK: - From a reverse-geocoded place

    /// The county a place is in, from the names iOS's geocoder gives
    /// (subAdministrativeArea "Travis County", administrativeArea "TX"; for
    /// an independent city, the locality).
    public func match(county: String?, locality: String?, state: String?) -> County? {
        guard let st = state.flatMap(findState) else { return nil }
        let inState = counties.filter { $0.state == st }
        if let county {
            let key = Self.bare(county)
            if let c = inState.first(where: { Self.bare($0.name) == key }) {
                return c
            }
        }
        if let locality {
            let key = Self.bare(locality)
            return inState.first { Self.bare($0.name) == key && $0.name.lowercased().hasSuffix("city") }
                ?? inState.first { Self.bare($0.name) == key }
        }
        return nil
    }

    static func normalize(_ s: String) -> String {
        let folded = s.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: Locale(identifier: "en_US"))
        let cleaned = folded.map { $0.isLetter || $0.isNumber ? $0 : " " }
        return String(cleaned).split(separator: " ").joined(separator: " ")
    }

    /// A county's name without its kind: "travis", "orleans", "richmond".
    static func bare(_ s: String) -> String {
        var n = normalize(s)
        for suffix in [" city and borough", " census area", " municipality", " municipio", " borough",
                       " county", " parish", " city", " district", " island", " islands"] where n.hasSuffix(suffix) {
            n = String(n.dropLast(suffix.count))
            break
        }
        return n.replacingOccurrences(of: "saint ", with: "st ")
    }
}
