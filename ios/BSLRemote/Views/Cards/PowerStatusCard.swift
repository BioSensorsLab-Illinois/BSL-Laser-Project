import SwiftUI
import BSLProtocol

struct PowerStatusCard: View {
    @Environment(DeviceSession.self) private var session

    private var rails: RailsBlock { session.snapshot.rails }
    private var pd: PdStatus { session.snapshot.pd }
    private var sess: SessionStatus { session.snapshot.session }
    private var stale: Bool { session.isStale }

    var body: some View {
        CardShell {
            HStack {
                Text("Power")
                Spacer()
                PillIndicator(label: powerTierLabel(sess.powerTier), on: sess.powerTier == .full, onColor: .green)
            }
        } content: {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 8) {
                    PillIndicator(label: "LD EN", on: rails.ld.enabled)
                    PillIndicator(label: "LD PG", on: rails.ld.pgood)
                    PillIndicator(label: "TEC EN", on: rails.tec.enabled)
                    PillIndicator(label: "TEC PG", on: rails.tec.pgood)
                }

                ReadoutRow(label: "Session", value: stateLabel(sess.state), stale: stale)
                ReadoutRow(label: "PD contract", value: pd.contractValid ? "valid" : "invalid", tone: pd.contractValid ? .good : .warn, stale: stale)
                ReadoutRow(label: "Negotiated", value: String(format: "%.1f W", pd.negotiatedPowerW), stale: stale)
                ReadoutRow(label: "Source", value: String(format: "%.1f V × %.2f A", pd.sourceVoltageV, pd.sourceCurrentA), stale: stale)
            }
        }
    }

    private func powerTierLabel(_ tier: PowerTier) -> String {
        switch tier {
        case .unknown: return "tier: unknown"
        case .programmingOnly: return "programming"
        case .insufficient: return "insufficient"
        case .reduced: return "reduced"
        case .full: return "full"
        case .other: return "other"
        }
    }

    private func stateLabel(_ state: SystemState) -> String {
        switch state {
        case .bootInit: return "BOOT_INIT"
        case .programmingOnly: return "PROGRAMMING_ONLY"
        case .safeIdle: return "SAFE_IDLE"
        case .powerNegotiation: return "POWER_NEGOTIATION"
        case .limitedPowerIdle: return "LIMITED_POWER_IDLE"
        case .tecWarmup: return "TEC_WARMUP"
        case .tecSettling: return "TEC_SETTLING"
        case .readyAlignment: return "READY_ALIGNMENT"
        case .readyNir: return "READY_NIR"
        case .alignmentActive: return "ALIGNMENT_ACTIVE"
        case .nirActive: return "NIR_ACTIVE"
        case .faultLatched: return "FAULT_LATCHED"
        case .serviceMode: return "SERVICE_MODE"
        case .unknown: return "UNKNOWN"
        }
    }
}
