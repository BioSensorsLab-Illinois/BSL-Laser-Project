import Foundation

/// Wavelength <-> temperature <-> TEC-voltage LUT.
///
/// Swift port of `host-console/src/lib/tec-calibration.ts`. Kept in
/// `BSLProtocol` so the host console and the iOS app share one authoritative
/// calibration curve — see `docs/protocol-spec.md` and the firmware default
/// LUT in `components/laser_controller/src/laser_controller_config.c:7-43`.
///
/// The data points were measured on the bench. Rows where temperature was
/// not directly captured are recovered by inferring from the TEC voltage
/// against the voltage-temperature sub-table.
public enum TecCalibration {

    public struct Point: Sendable, Equatable {
        public let tempC: Double
        public let tecVoltageV: Double
        public let wavelengthNm: Double
    }

    private static let tempVoltagePoints: [(tempC: Double, tecVoltageV: Double)] = [
        (5.0, 0.11),
        (6.0, 0.18),
        (7.5, 0.221),
        (10.7, 0.316),
        (15.5, 0.473),
        (20.0, 0.606),
        (20.5, 0.63),
        (28.2, 0.957),
        (29.0, 0.985),
        (37.4, 1.345),
        (44.7, 1.64),
        (46.1, 1.7),
        (54.5, 2.03),
        (58.7, 2.182),
        (65.0, 2.429),
    ]

    /// Calibration points sorted by temperature. Source of truth for every
    /// conversion below.
    public static let points: [Point] = {
        let rawPoints: [(tempC: Double?, tecVoltageV: Double, wavelengthNm: Double)] = [
            (5.0, 0.11, 771.2),
            (7.5, 0.221, 771.8),
            (10.7, 0.316, 772.8),
            (15.5, 0.473, 774.8),
            (20.0, 0.606, 776.8),
            (20.5, 0.63, 776.9),
            (nil, 0.8, 778.4),
            (28.2, 0.957, 779.8),
            (29.0, 0.985, 780.1),
            (nil, 1.224, 780.5),
            (37.4, 1.345, 781.6),
            (nil, 1.511, 781.6),
            (44.7, 1.64, 783.0),
            (46.1, 1.7, 783.1),
            (nil, 1.8, 783.2),
            (nil, 1.9, 784.2),
            (54.5, 2.03, 784.6),
            (58.7, 2.182, 786.1),
            (nil, 2.235, 787.4),
            (65.0, 2.429, 790.0),
        ]
        return rawPoints
            .map { raw in
                Point(
                    tempC: raw.tempC ?? inferTempFromVoltage(raw.tecVoltageV),
                    tecVoltageV: raw.tecVoltageV,
                    wavelengthNm: raw.wavelengthNm
                )
            }
            .sorted { $0.tempC < $1.tempC }
    }()

    /// Clamp `tempC` to the calibration window.
    public static func clampTempC(_ tempC: Double) -> Double {
        clamp(tempC, points.first?.tempC ?? 0, points.last?.tempC ?? 0)
    }

    /// Clamp `wavelengthNm` to the calibration window.
    public static func clampWavelengthNm(_ wavelengthNm: Double) -> Double {
        let sorted = points.sorted { $0.wavelengthNm < $1.wavelengthNm }
        return clamp(wavelengthNm, sorted.first?.wavelengthNm ?? 0, sorted.last?.wavelengthNm ?? 0)
    }

    /// Estimate the TEC target temperature (°C) for a given wavelength (nm).
    /// Uses linear interpolation across the bench-measured LUT. Mirrors the
    /// firmware wavelength LUT exactly.
    public static func tempFromWavelengthNm(_ wavelengthNm: Double) -> Double {
        let table = points
            .map { ($0.wavelengthNm, $0.tempC) }
            .sorted { $0.0 < $1.0 }
        return interpolate(table, clampWavelengthNm(wavelengthNm))
    }

    /// Estimate the wavelength (nm) that corresponds to a given target
    /// temperature (°C).
    public static func wavelengthFromTempC(_ tempC: Double) -> Double {
        let table = points
            .map { ($0.tempC, $0.wavelengthNm) }
            .sorted { $0.0 < $1.0 }
        return interpolate(table, clampTempC(tempC))
    }

    /// Estimate the TEC command voltage for a given target temperature (°C).
    public static func tecVoltageFromTempC(_ tempC: Double) -> Double {
        let table = points
            .map { ($0.tempC, $0.tecVoltageV) }
            .sorted { $0.0 < $1.0 }
        return interpolate(table, clampTempC(tempC))
    }

    private static func inferTempFromVoltage(_ voltage: Double) -> Double {
        let table = tempVoltagePoints
            .map { ($0.tecVoltageV, $0.tempC) }
            .sorted { $0.0 < $1.0 }
        return interpolate(table, voltage)
    }

    private static func interpolate(_ table: [(Double, Double)], _ value: Double) -> Double {
        guard !table.isEmpty else { return 0 }
        if value <= table[0].0 { return table[0].1 }
        if let last = table.last, value >= last.0 { return last.1 }
        for i in 1..<table.count {
            let prev = table[i - 1]
            let cur = table[i]
            if value <= cur.0 {
                let span = cur.0 - prev.0
                let ratio = span == 0 ? 0 : (value - prev.0) / span
                return prev.1 + (cur.1 - prev.1) * ratio
            }
        }
        return table.last?.1 ?? 0
    }

    private static func clamp(_ v: Double, _ lo: Double, _ hi: Double) -> Double {
        max(lo, min(hi, v))
    }
}
