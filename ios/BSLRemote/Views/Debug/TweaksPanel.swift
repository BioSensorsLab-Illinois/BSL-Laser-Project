#if DEBUG
import SwiftUI
import BSLProtocol

/// Debug-only floating panel that lets an engineer pretend the firmware is
/// in a given state, without a real controller. Mirrors the HTML tweaks
/// panel at `bsl-laser-system/project/BSL Laser Controller.html:2027-2130`.
///
/// This is UI-side only: it mutates an `@Observable` override that
/// `DeviceSession` consults. In RELEASE builds the panel is compiled out
/// entirely.
@MainActor
@Observable
final class DebugTweaks {
    var enabled: Bool = false

    var forceConnected: Bool = true
    var forceMode: LaserMode = .disarmed
    var forceFault: FaultKind = .none

    enum FaultKind: String, CaseIterable { case none, caution, warn }

    func synthesizedSnapshot(from base: DeviceSnapshot) -> DeviceSnapshot {
        guard enabled else { return base }
        var snap = base
        switch forceMode {
        case .disarmed:
            snap.deployment.active = false
            snap.deployment.ready = false
            snap.deployment.readyIdle = false
            snap.laser.measuredCurrentA = 0
        case .armed:
            snap.deployment.active = true
            snap.deployment.ready = true
            snap.deployment.readyIdle = true
            snap.laser.measuredCurrentA = 0
        case .lasing:
            snap.deployment.active = true
            snap.deployment.ready = true
            snap.deployment.readyIdle = true
            snap.laser.measuredCurrentA = max(snap.bench.requestedCurrentA, 1.0)
        }
        switch forceFault {
        case .none:
            snap.fault.latched = false
        case .caution:
            snap.safety.tecTempAdcBlocked = true
        case .warn:
            snap.fault.latched = true
            snap.fault.latchedCode = "ld_overtemp"
            snap.fault.latchedReason = "Driver IC temperature exceeded limit"
        }
        return snap
    }
}

struct TweaksPanel: View {
    @Environment(\.bslTheme) private var t
    var tweaks: DebugTweaks

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("TWEAKS · DEBUG")
                    .font(.system(size: 11, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                Spacer()
                Toggle("", isOn: Bindable(tweaks).enabled)
                    .labelsHidden()
                    .tint(BSL.orange)
            }
            if tweaks.enabled {
                row("Connected") {
                    Toggle("", isOn: Bindable(tweaks).forceConnected)
                        .labelsHidden().tint(BSL.ok)
                }
                row("Mode") {
                    HStack(spacing: 4) {
                        ForEach([LaserMode.disarmed, .armed, .lasing], id: \.self) { m in
                            let on = tweaks.forceMode == m
                            Button { tweaks.forceMode = m } label: {
                                Text(m.shortLabel)
                                    .font(.system(size: 10, weight: .heavy))
                                    .padding(.horizontal, 8).padding(.vertical, 5)
                                    .background(on ? BSL.orange : .clear)
                                    .foregroundStyle(on ? .white : t.ink)
                                    .clipShape(RoundedRectangle(cornerRadius: 6))
                                    .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(on ? .clear : t.border, lineWidth: 0.5))
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
                row("Fault") {
                    HStack(spacing: 4) {
                        ForEach(DebugTweaks.FaultKind.allCases, id: \.self) { f in
                            let on = tweaks.forceFault == f
                            Button { tweaks.forceFault = f } label: {
                                Text(f.rawValue.uppercased())
                                    .font(.system(size: 10, weight: .heavy))
                                    .padding(.horizontal, 8).padding(.vertical, 5)
                                    .background(on ? BSL.orange : .clear)
                                    .foregroundStyle(on ? .white : t.ink)
                                    .clipShape(RoundedRectangle(cornerRadius: 6))
                                    .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(on ? .clear : t.border, lineWidth: 0.5))
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
            }
        }
        .padding(14)
        .frame(width: 260)
        .background(t.surface)
        .clipShape(RoundedRectangle(cornerRadius: 14))
        .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(t.border, lineWidth: 0.5))
        .shadow(color: .black.opacity(0.18), radius: 18, x: 0, y: 8)
    }

    @ViewBuilder private func row<C: View>(_ label: String, @ViewBuilder control: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label.uppercased())
                .font(.system(size: 9, weight: .bold))
                .tracking(0.6)
                .foregroundStyle(t.muted)
            control()
        }
    }
}
#endif
