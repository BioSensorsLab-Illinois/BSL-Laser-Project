import SwiftUI
import BSLProtocol

/// Aggregates every auto-clearing interlock into a single yellow list. Per
/// `docs/protocol-spec.md:627-628`: IMU/ToF invalid/stale and lambda/TEC-ADC
/// trips are auto-clear. Horizon and distance block NIR while the condition
/// holds and self-clear when it clears.
struct MasterCautionCard: View {
    @Environment(DeviceSession.self) private var session

    private var snap: DeviceSnapshot { session.snapshot }
    private var safety: SafetyStatus { snap.safety }
    private var imu: ImuStatus { snap.imu }
    private var tof: TofStatus { snap.tof }
    private var fault: FaultSummary { snap.fault }
    private var locks: InterlockEnableMask { safety.interlocks }

    var body: some View {
        CardShell {
            HStack {
                Text("Master Caution")
                Spacer()
                PillIndicator(
                    label: activeCount == 0 ? "CLEAR" : "\(activeCount) active",
                    on: activeCount > 0,
                    onColor: .orange
                )
            }
        } content: {
            if activeCount == 0 {
                Text("No auto-clearing interlocks active.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            } else {
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(rows) { row in
                        HStack(alignment: .top, spacing: 8) {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundStyle(.orange)
                                .font(.caption)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(row.title).font(.footnote.weight(.medium))
                                Text(row.detail).font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            if row.masked {
                                Text("MASKED")
                                    .font(.caption2.weight(.semibold))
                                    .padding(.horizontal, 6)
                                    .padding(.vertical, 2)
                                    .background(Color(.tertiarySystemBackground))
                                    .clipShape(Capsule())
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            }
        }
    }

    private struct Row: Identifiable {
        let id = UUID()
        let title: String
        let detail: String
        let masked: Bool
    }

    private var rows: [Row] {
        var r: [Row] = []
        if safety.horizonBlocked {
            r.append(Row(title: "Horizon", detail: "Beam pitch/roll outside the allowed cone.", masked: !locks.horizonEnabled))
        }
        if safety.distanceBlocked {
            r.append(Row(title: "Distance", detail: String(format: "ToF distance %.2f m outside [%.2f–%.2f m].", tof.distanceM, safety.tofMinRangeM, safety.tofMaxRangeM), masked: !locks.distanceEnabled))
        }
        if safety.lambdaDriftBlocked {
            r.append(Row(title: "Lambda drift", detail: String(format: "Drift %+.2f nm > limit %.2f nm (hold %d ms).", safety.lambdaDriftNm, safety.lambdaDriftLimitNm, safety.lambdaDriftHoldMs), masked: !locks.lambdaDriftEnabled))
        }
        if safety.tecTempAdcBlocked {
            r.append(Row(title: "TEC ADC trip", detail: String(format: "ADC %.3f V ≥ trip %.3f V (hold %d ms).", safety.tempAdcVoltageV, safety.tecTempAdcTripV, safety.tecTempAdcHoldMs), masked: !locks.tecTempAdcEnabled))
        }
        if !imu.valid {
            r.append(Row(title: "IMU invalid", detail: "The IMU reports invalid data.", masked: !locks.imuInvalidEnabled))
        }
        if !imu.fresh {
            r.append(Row(title: "IMU stale", detail: "IMU sample older than stale threshold.", masked: !locks.imuStaleEnabled))
        }
        if !tof.valid {
            r.append(Row(title: "ToF invalid", detail: "ToF reports invalid data.", masked: !locks.tofInvalidEnabled))
        }
        if !tof.fresh {
            r.append(Row(title: "ToF stale", detail: "ToF sample older than stale threshold.", masked: !locks.tofStaleEnabled))
        }
        if fault.isAutoClearing {
            r.append(Row(title: "Active: \(fault.activeCode)", detail: fault.activeReason.isEmpty ? "Auto-clearing interlock." : fault.activeReason, masked: false))
        }
        return r
    }

    private var activeCount: Int { rows.count }
}
