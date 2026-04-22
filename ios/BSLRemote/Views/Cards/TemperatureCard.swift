import SwiftUI
import BSLProtocol

/// TEC / LD temperature card. TEC temp is the laser-diode junction via the
/// TEC-plate thermistor; `driverTempC` is the ATLS6A214 driver IC.
struct TemperatureCard: View {
    @Environment(DeviceSession.self) private var session

    private var tec: TecStatus { session.snapshot.tec }
    private var laser: LaserStatus { session.snapshot.laser }
    private var safety: SafetyStatus { session.snapshot.safety }
    private var stale: Bool { session.isStale }
    /// 2026-04-20: distinguish "no snapshot ever received" from
    /// "snapshot is stale". Before first frame lands, firmware-derived
    /// readings are all decoder defaults (TEC target 25 °C, LD temp 0
    /// °C) — displaying those as if real was the source of the "LD
    /// temp initial target readback is wrong" report. We now show "—"
    /// until the first frame arrives.
    private var noSnapshotYet: Bool { session.lastFrameAt == nil }

    var body: some View {
        CardShell {
            HStack {
                Text("Temperature")
                Spacer()
                PillIndicator(label: tec.tempGood ? "SETTLED" : "SETTLING", on: tec.tempGood)
                if safety.tecTempAdcBlocked {
                    PillIndicator(label: "ADC BLOCK", on: true, onColor: .orange)
                }
            }
        } content: {
            VStack(spacing: 10) {
                ReadoutRow(
                    label: "TEC actual",
                    value: noSnapshotYet ? "—" : String(format: "%.2f °C", tec.tempC),
                    tone: tec.tempGood ? .good : .warn,
                    stale: stale || noSnapshotYet)
                ReadoutRow(
                    label: "TEC target",
                    value: noSnapshotYet ? "—" : String(format: "%.2f °C", tec.targetTempC),
                    stale: stale || noSnapshotYet)
                ReadoutRow(
                    label: "Tolerance",
                    value: noSnapshotYet ? "—" : String(format: "±%.2f °C", safety.tecReadyToleranceC),
                    stale: stale || noSnapshotYet)
                ReadoutRow(
                    label: "Driver IC",
                    value: (noSnapshotYet || !laser.loopGood) ? "—" : String(format: "%.1f °C", laser.driverTempC),
                    tone: laser.driverTempC > safety.ldOvertempLimitC ? .bad : .normal,
                    stale: stale || noSnapshotYet)
                if tec.settlingSecondsRemaining > 0 {
                    ReadoutRow(label: "Settling in", value: "\(tec.settlingSecondsRemaining) s", stale: stale)
                }
            }
        }
    }
}
