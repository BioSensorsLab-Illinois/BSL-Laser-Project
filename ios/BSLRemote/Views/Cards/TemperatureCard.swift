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
                ReadoutRow(label: "TEC actual", value: String(format: "%.2f °C", tec.tempC), tone: tec.tempGood ? .good : .warn, stale: stale)
                ReadoutRow(label: "TEC target", value: String(format: "%.2f °C", tec.targetTempC), stale: stale)
                ReadoutRow(label: "Tolerance", value: String(format: "±%.2f °C", safety.tecReadyToleranceC), stale: stale)
                ReadoutRow(label: "Driver IC", value: String(format: "%.1f °C", laser.driverTempC), tone: laser.driverTempC > safety.ldOvertempLimitC ? .bad : .normal, stale: stale)
                if tec.settlingSecondsRemaining > 0 {
                    ReadoutRow(label: "Settling in", value: "\(tec.settlingSecondsRemaining) s", stale: stale)
                }
            }
        }
    }
}
