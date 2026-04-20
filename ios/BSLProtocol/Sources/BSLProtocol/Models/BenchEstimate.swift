import Foundation

/// System power-draw estimator.
///
/// Swift port of `host-console/src/lib/bench-model.ts`. Kept in `BSLProtocol`
/// so the iOS app renders the same numbers as the desktop console for
/// USB-PD draw, TEC input power, and headroom. One deliberate difference:
/// the desktop console renders `buttonBoard.ledOwned` (a field the iOS app
/// does NOT currently decode — it would be clobbered by the snapshot merge
/// layer) so the ToF LED term uses the bench-side `illuminationDutyCyclePct`
/// and `requestedLedDutyCyclePct` fields only.
public enum BenchEstimate {

    public static let laserFullCurrentA: Double = 5.2
    public static let laserForwardVoltageV: Double = 3.0
    public static let laserFullOpticalPowerW: Double = 5.0
    public static let driverEfficiency: Double = 0.9
    public static let tecEfficiency: Double = 0.9
    public static let greenAlignmentPowerW: Double = 0.4
    public static let tofLedPowerAtFullW: Double = 6.0

    /// Baseline power drawn by the controller board, MCU, Wi-Fi, peripheral
    /// supplies, and fans when the laser/TEC/LED are all idle. Matches the
    /// user-directed 1 W idle floor that the desktop GUI now adds
    /// (2026-04-19).
    public static let idlePowerW: Double = 1.0

    public struct Estimate: Sendable, Equatable {
        public let commandedOpticalPowerW: Double
        public let averageOpticalPowerW: Double
        public let laserElectricalPowerW: Double
        public let laserInputPowerW: Double
        public let tecElectricalPowerW: Double
        public let tecInputPowerW: Double
        public let tecCoolingPowerW: Double
        public let greenAlignmentInputPowerW: Double
        public let tofLedInputPowerW: Double
        public let idlePowerW: Double
        public let totalEstimatedInputPowerW: Double
        public let pdHeadroomW: Double
    }

    public static func derive(from snapshot: DeviceSnapshot) -> Estimate {
        let highCurrentA = clamp(snapshot.laser.commandedCurrentA, 0, laserFullCurrentA)
        let dutyFraction: Double = snapshot.laser.nirEnabled ? 1.0 : 0.0
        let commandedOpticalPowerW = opticalPowerFromCurrentA(highCurrentA)
        let averageCurrentA = snapshot.laser.nirEnabled ? dutyFraction * highCurrentA : 0.0
        let averageOpticalPowerW = snapshot.laser.nirEnabled
            ? dutyFraction * opticalPowerFromCurrentA(highCurrentA)
            : 0.0
        let laserElectricalPowerW = averageCurrentA * laserForwardVoltageV
        let laserInputPowerW = laserElectricalPowerW > 0
            ? laserElectricalPowerW / driverEfficiency
            : 0.0

        let tecElectricalPowerW = abs(snapshot.tec.currentA * snapshot.tec.voltageV)
        let tecInputPowerW = tecElectricalPowerW > 0
            ? tecElectricalPowerW / tecEfficiency
            : 0.0
        let tecCoolingPowerW = tecElectricalPowerW * tecEfficiency

        let greenAlignmentInputPowerW = snapshot.laser.alignmentEnabled
            ? greenAlignmentPowerW
            : 0.0

        // ToF-board front LED draw. 50% duty → 3 W, 100% → 6 W (user spec
        // 2026-04-16). On iOS we derive the active duty from whichever
        // bench request is asserting it.
        let bench = snapshot.bench
        let ledDutyPct: Int
        if bench.illuminationEnabled {
            ledDutyPct = bench.illuminationDutyCyclePct
        } else if bench.requestedLedEnabled {
            ledDutyPct = bench.requestedLedDutyCyclePct
        } else {
            ledDutyPct = 0
        }
        let tofLedInputPowerW = (Double(clamp(ledDutyPct, 0, 100)) / 100.0) * tofLedPowerAtFullW

        let totalEstimatedInputPowerW = laserInputPowerW
            + tecInputPowerW
            + greenAlignmentInputPowerW
            + tofLedInputPowerW
            + idlePowerW

        let pdHeadroomW = snapshot.pd.negotiatedPowerW - totalEstimatedInputPowerW

        return Estimate(
            commandedOpticalPowerW: commandedOpticalPowerW,
            averageOpticalPowerW: averageOpticalPowerW,
            laserElectricalPowerW: laserElectricalPowerW,
            laserInputPowerW: laserInputPowerW,
            tecElectricalPowerW: tecElectricalPowerW,
            tecInputPowerW: tecInputPowerW,
            tecCoolingPowerW: tecCoolingPowerW,
            greenAlignmentInputPowerW: greenAlignmentInputPowerW,
            tofLedInputPowerW: tofLedInputPowerW,
            idlePowerW: idlePowerW,
            totalEstimatedInputPowerW: totalEstimatedInputPowerW,
            pdHeadroomW: pdHeadroomW
        )
    }

    public static func opticalPowerFromCurrentA(_ currentA: Double) -> Double {
        clamp(currentA, 0, laserFullCurrentA) * (laserFullOpticalPowerW / laserFullCurrentA)
    }

    private static func clamp(_ v: Double, _ lo: Double, _ hi: Double) -> Double {
        max(lo, min(hi, v))
    }

    private static func clamp(_ v: Int, _ lo: Int, _ hi: Int) -> Int {
        max(lo, min(hi, v))
    }
}
