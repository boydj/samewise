/// POSIX TZ strings, a port of the firmware's services/ble/tz.c: the parser
/// (std offset [dst [offset] ,rule,rule], names of 3 to 15 letters or
/// <quoted> with digits and signs, offsets up to 24 hours, rule times from
/// -167 to 167 hours; a daylight-saving name without rules is rejected) and
/// tz_local(), the radio's UTC to local time conversion.
public enum PosixTimeZone {
    public enum RuleKind: Equatable, Sendable {
        /// Jn: 1-365, 29 February never counted.
        case julian
        /// n: 0-365, 29 February counted.
        case zeroDay
        /// Mm.w.d.
        case month
    }

    public struct Rule: Equatable, Sendable {
        public var kind: RuleKind = .month
        public var month = 0
        /// 1-5, 5 = last.
        public var week = 0
        /// 0 = Sunday.
        public var wday = 0
        public var day = 0
        /// Local wall time of the change, default 02:00.
        public var timeS = 2 * 3600
    }

    public struct Info: Equatable, Sendable {
        public var stdName = ""
        public var dstName = ""
        /// local = UTC + offset (EST5 gives -18000).
        public var stdOffsetS = 0
        public var dstOffsetS = 0
        public var hasDst = false
        /// In standard time.
        public var start = Rule()
        /// In daylight time.
        public var end = Rule()

        /// The radio's local wall-clock seconds for a UTC time, and whether
        /// daylight-saving time applied (tz_local()).
        public func local(utc: Int64) -> (seconds: Int64, dst: Bool) {
            var inDst = false
            if hasDst {
                let y = yearOf(utc + Int64(stdOffsetS))
                let s = ruleDay(start, y) + Int64(start.timeS - stdOffsetS)
                let e = ruleDay(end, y) + Int64(end.timeS - dstOffsetS)
                inDst = s < e ? (utc >= s && utc < e) : !(utc >= e && utc < s)
            }
            return (utc + Int64(inDst ? dstOffsetS : stdOffsetS), inDst)
        }
    }

    public static func isValid(_ s: String) -> Bool {
        parse(s) != nil
    }

    /// The parsed string, or nil for anything the radio would reject.
    public static func parse(_ s: String) -> Info? {
        var p = Parser(Array(s.utf8))
        return p.parse()
    }

    struct Parser {
        let c: [UInt8]
        var i = 0

        init(_ c: [UInt8]) {
            self.c = c
        }

        var peek: UInt8? { i < c.count ? c[i] : nil }

        static func isAlpha(_ b: UInt8) -> Bool { (b >= 0x41 && b <= 0x5A) || (b >= 0x61 && b <= 0x7A) }

        static func isDigit(_ b: UInt8) -> Bool { b >= 0x30 && b <= 0x39 }

        mutating func eat(_ b: UInt8) -> Bool {
            guard peek == b else { return false }
            i += 1
            return true
        }

        /// 1 to maxDigits digits, not followed by another digit.
        mutating func number(_ maxDigits: Int) -> Int? {
            var v = 0
            var n = 0
            while n < maxDigits, let b = peek, Parser.isDigit(b) {
                v = v * 10 + Int(b - 0x30)
                i += 1
                n += 1
            }
            if n == 0 { return nil }
            if let b = peek, Parser.isDigit(b) { return nil }
            return v
        }

        mutating func name() -> String? {
            let quoted = eat(0x3C)  // <
            var out: [UInt8] = []
            while let b = peek,
                  quoted ? (Parser.isAlpha(b) || Parser.isDigit(b) || b == 0x2B || b == 0x2D)
                         : Parser.isAlpha(b) {
                if out.count == 15 { return nil }
                out.append(b)
                i += 1
            }
            if quoted && !eat(0x3E) { return nil }  // >
            return out.count >= 3 ? String(decoding: out, as: UTF8.self) : nil
        }

        /// [+|-]hh[:mm[:ss]] with hh up to maxHours, in seconds.
        mutating func hms(_ maxHours: Int) -> Int? {
            var sign = 1
            if eat(0x2D) {
                sign = -1
            } else {
                _ = eat(0x2B)
            }
            guard let h = number(3), h <= maxHours else { return nil }
            var m = 0
            var s = 0
            if eat(0x3A) {
                guard let mm = number(2), mm <= 59 else { return nil }
                m = mm
                if eat(0x3A) {
                    guard let ss = number(2), ss <= 59 else { return nil }
                    s = ss
                }
            }
            return sign * (h * 3600 + m * 60 + s)
        }

        mutating func rule() -> Rule? {
            var r = Rule()
            if eat(0x4D) {  // Mm.w.d
                guard let m = number(2), (1...12).contains(m), eat(0x2E),
                      let w = number(1), (1...5).contains(w), eat(0x2E),
                      let d = number(1), d <= 6 else { return nil }
                r.kind = .month
                r.month = m
                r.week = w
                r.wday = d
            } else if eat(0x4A) {  // Jn
                guard let n = number(3), (1...365).contains(n) else { return nil }
                r.kind = .julian
                r.day = n
            } else {  // n
                guard let n = number(3), n <= 365 else { return nil }
                r.kind = .zeroDay
                r.day = n
            }
            if eat(0x2F) {  // /time
                guard let t = hms(167) else { return nil }
                r.timeS = t
            }
            return r
        }

        mutating func parse() -> Info? {
            var t = Info()
            guard let std = name(), let off = hms(24) else { return nil }
            t.stdName = std
            t.stdOffsetS = -off
            if peek == nil { return t }
            guard let dst = name() else { return nil }
            t.dstName = dst
            t.hasDst = true
            t.dstOffsetS = t.stdOffsetS + 3600
            if peek != 0x2C {
                guard let off = hms(24) else { return nil }
                t.dstOffsetS = -off
            }
            guard eat(0x2C), let start = rule(), eat(0x2C), let end = rule(), peek == nil else { return nil }
            t.start = start
            t.end = end
            return t
        }
    }

    // MARK: - Calendar, as services/match/same_time.c

    static let dayS: Int64 = 86400

    static func leap(_ y: Int64) -> Bool { (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 }

    static func monthDays(_ y: Int64, _ m: Int) -> Int64 {
        let days: [Int64] = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
        return m == 2 && leap(y) ? 29 : days[m - 1]
    }

    static func daysFromCivil(_ year: Int64, _ m: Int, _ d: Int) -> Int64 {
        let y = year - (m <= 2 ? 1 : 0)
        let era = (y >= 0 ? y : y - 399) / 400
        let yoe = y - era * 400
        let doy = (153 * Int64(m > 2 ? m - 3 : m + 9) + 2) / 5 + Int64(d) - 1
        let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy
        return era * 146097 + doe - 719468
    }

    static func yearOf(_ utc: Int64) -> Int64 {
        let z = (utc >= 0 ? utc : utc - (dayS - 1)) / dayS + 719468
        let era = (z >= 0 ? z : z - 146096) / 146097
        let doe = z - era * 146097
        let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
        let mp = (5 * doy + 2) / 153
        return yoe + era * 400 + (mp >= 10 ? 1 : 0)
    }

    /// Seconds from the epoch to the local midnight starting the rule's day in year y.
    static func ruleDay(_ r: Rule, _ y: Int64) -> Int64 {
        let jan1 = daysFromCivil(y, 1, 1)
        switch r.kind {
        case .julian:
            return (jan1 + Int64(r.day) - 1 + (leap(y) && r.day >= 60 ? 1 : 0)) * dayS
        case .zeroDay:
            return (jan1 + Int64(r.day)) * dayS
        case .month:
            let first = daysFromCivil(y, r.month, 1)
            let wday1 = ((first % 7) + 7 + 4) % 7  // 1970-01-01 was a Thursday
            var d = (Int64(r.wday) - wday1 + 7) % 7 + 7 * Int64(r.week - 1)
            if d >= monthDays(y, r.month) {
                d -= 7
            }
            return (first + d) * dayS
        }
    }
}
