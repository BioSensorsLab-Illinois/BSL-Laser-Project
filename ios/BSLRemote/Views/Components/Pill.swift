import SwiftUI

/// Semantic pill. Tones mirror
/// `bsl-laser-system/project/BSL Laser Controller.html:482-500`.
struct BSLPill<Content: View>: View {
    enum Tone { case neutral, ok, caution, warn, brand, nir }
    let tone: Tone
    let content: Content

    init(_ tone: Tone = .neutral, @ViewBuilder content: () -> Content) {
        self.tone = tone
        self.content = content()
    }

    @Environment(\.bslTheme) private var t

    var body: some View {
        HStack(spacing: 6) {
            content
        }
        .font(.system(size: 11, weight: .semibold, design: .default))
        .tracking(0.2)
        .padding(.horizontal, 9)
        .padding(.vertical, 4)
        .background(bg)
        .foregroundStyle(fg)
        .clipShape(Capsule())
    }

    private var bg: Color {
        switch tone {
        case .neutral: return t.dark ? Color.white.opacity(0.06) : BSL.navy.opacity(0.06)
        case .ok:      return t.dark ? BSL.ok.opacity(0.15)   : BSL.okSoft
        case .caution: return t.dark ? BSL.caution.opacity(0.18) : BSL.cautionSoft
        case .warn:    return t.dark ? BSL.warn.opacity(0.18)  : BSL.warnSoft
        case .brand:   return t.dark ? BSL.orange.opacity(0.18) : BSL.orangeSoft
        case .nir:     return t.dark ? BSL.nir.opacity(0.20)    : BSL.nirSoft
        }
    }
    private var fg: Color {
        switch tone {
        case .neutral: return t.muted
        case .ok:      return t.dark ? Color(red: 0.306, green: 0.819, blue: 0.607) : Color(red: 0.054, green: 0.478, blue: 0.282)
        case .caution: return t.dark ? Color(red: 0.960, green: 0.764, blue: 0.419) : Color(red: 0.541, green: 0.352, blue: 0.000)
        case .warn:    return t.dark ? Color(red: 1.0,   green: 0.545, blue: 0.494) : Color(red: 0.607, green: 0.113, blue: 0.074)
        case .brand:   return t.dark ? Color(red: 1.0,   green: 0.701, blue: 0.541) : BSL.orangeDeep
        case .nir:     return t.dark ? Color(red: 0.780, green: 0.607, blue: 0.980) : Color(red: 0.396, green: 0.082, blue: 0.752)
        }
    }
}

/// Pulsing dot used inside pills and headers.
struct LiveDot: View {
    var color: Color = BSL.ok
    var size: CGFloat = 7
    @State private var pulse: Bool = false

    var body: some View {
        ZStack {
            Circle()
                .fill(color.opacity(pulse ? 0 : 0.4))
                .scaleEffect(pulse ? 2.2 : 1.0)
            Circle().fill(color)
        }
        .frame(width: size, height: size)
        .onAppear {
            withAnimation(.easeOut(duration: 2).repeatForever(autoreverses: false)) {
                pulse = true
            }
        }
    }
}
