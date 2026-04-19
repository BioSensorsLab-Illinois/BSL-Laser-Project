import SwiftUI
import BSLProtocol

/// Flashing master-warning banner / amber master-caution banner. Tap opens
/// the detailed MasterCaution / MasterWarning card sheet. Mirrors
/// `bsl-laser-system/project/BSL Laser Controller.html:691-719`.
struct FaultBanner: View {
    enum Kind { case caution, warning }

    let kind: Kind
    let title: String
    let detail: String
    let onTap: () -> Void

    @Environment(\.bslTheme) private var t
    @State private var flash: Bool = false

    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 10) {
                Image(systemName: kind == .warning ? "exclamationmark.circle.fill" : "exclamationmark.triangle.fill")
                    .font(.system(size: 20))
                    .foregroundStyle(fg)
                VStack(alignment: .leading, spacing: 1) {
                    Text(title)
                        .font(.system(size: 12, weight: .heavy))
                        .tracking(0.8)
                        .foregroundStyle(fg)
                    Text(detail)
                        .font(.system(size: 11))
                        .foregroundStyle(fg.opacity(0.85))
                        .lineLimit(2)
                }
                Spacer(minLength: 8)
                Image(systemName: "chevron.right")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(fg.opacity(0.7))
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .background(bg)
            .overlay(
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .strokeBorder(borderColor, lineWidth: 0.5)
            )
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            .opacity(kind == .warning && flash ? 0.55 : 1.0)
        }
        .buttonStyle(.plain)
        .onAppear {
            guard kind == .warning else { return }
            withAnimation(.easeInOut(duration: 0.7).repeatForever(autoreverses: true)) {
                flash = true
            }
        }
    }

    private var bg: Color {
        switch kind {
        case .warning: return t.dark ? BSL.warn.opacity(0.15) : BSL.warnSoft
        case .caution: return t.dark ? BSL.caution.opacity(0.15) : BSL.cautionSoft
        }
    }
    private var fg: Color {
        switch kind {
        case .warning: return t.dark ? Color(red: 1.0, green: 0.545, blue: 0.494) : Color(red: 0.607, green: 0.113, blue: 0.074)
        case .caution: return t.dark ? Color(red: 0.960, green: 0.764, blue: 0.419) : Color(red: 0.541, green: 0.352, blue: 0.000)
        }
    }
    private var borderColor: Color {
        switch kind {
        case .warning: return BSL.warn.opacity(0.35)
        case .caution: return BSL.caution.opacity(0.35)
        }
    }
}
