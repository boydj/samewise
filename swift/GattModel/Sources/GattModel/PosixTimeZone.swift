/// POSIX TZ string validation, a port of the firmware's services/ble/tz.c
/// parser: std offset [dst [offset] ,rule,rule], names of 3 to 15 letters
/// (or <quoted> with digits and signs), offsets up to 24 hours, rule times
/// from -167 to 167 hours. A daylight-saving name without rules is rejected.
public enum PosixTimeZone {
    public static func isValid(_ s: String) -> Bool {
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

        mutating func name() -> Bool {
            let quoted = eat(0x3C)  // <
            var n = 0
            while let b = peek,
                  quoted ? (Parser.isAlpha(b) || Parser.isDigit(b) || b == 0x2B || b == 0x2D)
                         : Parser.isAlpha(b) {
                if n == 15 { return false }
                n += 1
                i += 1
            }
            if quoted && !eat(0x3E) { return false }  // >
            return n >= 3
        }

        /// [+|-]hh[:mm[:ss]] with hh up to maxHours.
        mutating func hms(_ maxHours: Int) -> Bool {
            if !eat(0x2B) { _ = eat(0x2D) }
            guard let h = number(3), h <= maxHours else { return false }
            if eat(0x3A) {
                guard let m = number(2), m <= 59 else { return false }
                if eat(0x3A) {
                    guard let s = number(2), s <= 59 else { return false }
                }
            }
            return true
        }

        mutating func rule() -> Bool {
            if eat(0x4D) {  // Mm.w.d
                guard let m = number(2), (1...12).contains(m), eat(0x2E),
                      let w = number(1), (1...5).contains(w), eat(0x2E),
                      let d = number(1), d <= 6 else { return false }
            } else if eat(0x4A) {  // Jn
                guard let n = number(3), (1...365).contains(n) else { return false }
            } else {  // n
                guard let n = number(3), n <= 365 else { return false }
            }
            if eat(0x2F) {  // /time
                return hms(167)
            }
            return true
        }

        mutating func parse() -> Bool {
            guard name(), hms(24) else { return false }
            if peek == nil { return true }
            guard name() else { return false }
            if peek != 0x2C && !hms(24) { return false }
            guard eat(0x2C), rule(), eat(0x2C), rule() else { return false }
            return peek == nil
        }
    }
}
