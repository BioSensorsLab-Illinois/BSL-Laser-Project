import SwiftUI

/// First screen on every cold launch. User requirement (2026-04-19): shown
/// every time the app starts so the operator re-acknowledges Class-4 laser
/// hazards; future builds will host an operating-instruction / safety
/// animation in `instructionSlot`. Operator taps Continue to proceed to the
/// Connect flow. The gate is not persisted across launches — that is the
/// point.
struct WelcomeView: View {
    let onContinue: () -> Void

    @Environment(\.bslTheme) private var t
    @Environment(\.colorScheme) private var scheme

    /// Future safety / operating-instruction animation will drop in here.
    /// Keeping the slot as a `some View` avoids a re-plumb when that
    /// ship-readiness task lands (2026-04-19 directive).
    @ViewBuilder private var instructionSlot: some View {
        EmptyView()
    }

    var body: some View {
        GeometryReader { geo in
            ZStack {
                t.bg.ignoresSafeArea()
                backgroundBeam

                VStack(spacing: 0) {
                    Spacer(minLength: 24)
                    heroBlock
                    Spacer(minLength: 20)
                    safetyCard
                        .padding(.horizontal, 22)
                    Spacer(minLength: 24)
                    footerBlock
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .frame(maxWidth: 560)
                .padding(.horizontal, max(16, geo.size.width * 0.03))
            }
        }
    }

    // MARK: - Visual slices

    /// Soft orange radial glow behind the brand mark. Reads `scheme` rather
    /// than `t.dark` so the glow strengthens correctly when an Appearance
    /// override flips the scheme mid-session.
    private var backgroundBeam: some View {
        RadialGradient(
            colors: [
                BSL.orange.opacity(scheme == .dark ? 0.18 : 0.08),
                .clear,
            ],
            center: .init(x: 0.5, y: 0.28),
            startRadius: 10,
            endRadius: 320
        )
        .blur(radius: 10)
        .allowsHitTesting(false)
        .ignoresSafeArea()
    }

    private var heroBlock: some View {
        VStack(spacing: 28) {
            ApertureGlyph(size: 88)
                .shadow(color: scheme == .dark ? BSL.orange.opacity(0.40) : BSL.navy.opacity(0.35),
                        radius: 20, x: 0, y: 16)

            VStack(spacing: 12) {
                Text("BSL · HHLS")
                    .font(.system(size: 11, weight: .heavy))
                    .tracking(3)
                    .foregroundStyle(BSL.orange)

                Text("Hand-held\nlaser controller")
                    .font(.system(size: 28, weight: .bold))
                    .tracking(-0.5)
                    .foregroundStyle(t.ink)
                    .multilineTextAlignment(.center)
                    .lineSpacing(2)

                Text("Operator interface for the BSL benchtop source. Firmware is the authoritative safety gate — this app surfaces state and requests actions.")
                    .font(.system(size: 14))
                    .foregroundStyle(t.muted)
                    .lineSpacing(3)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: 320)
            }

            instructionSlot
        }
        .padding(.horizontal, 32)
    }

    private var safetyCard: some View {
        HStack(alignment: .top, spacing: 10) {
            ZStack {
                RoundedRectangle(cornerRadius: 4, style: .continuous)
                    .fill(BSL.caution)
                Text("!")
                    .font(.system(size: 12, weight: .heavy))
                    .foregroundStyle(.white)
            }
            .frame(width: 20, height: 20)

            VStack(alignment: .leading, spacing: 2) {
                Text("Class 4 invisible laser hazard.")
                    .font(.system(size: 11, weight: .bold))
                    .foregroundStyle(scheme == .dark
                                      ? Color(red: 0.96, green: 0.76, blue: 0.42)
                                      : Color(red: 0.541, green: 0.352, blue: 0.000))
                Text("Use only with proper OD7+ eyewear and an interlocked enclosure. Emission is not eye-safe.")
                    .font(.system(size: 11))
                    .foregroundStyle(scheme == .dark
                                      ? Color(red: 0.96, green: 0.76, blue: 0.42).opacity(0.9)
                                      : Color(red: 0.541, green: 0.352, blue: 0.000))
                    .lineSpacing(2)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
        .background(scheme == .dark ? BSL.caution.opacity(0.10) : BSL.caution.opacity(0.12))
        .overlay(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .strokeBorder(BSL.caution.opacity(scheme == .dark ? 0.35 : 0.45), lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        .frame(maxWidth: 340)
    }

    private var footerBlock: some View {
        VStack(spacing: 12) {
            BSLPrimaryButton(title: "Continue", action: onContinue)
                .frame(maxWidth: 360)
            Text(versionLine)
                .font(.system(size: 10))
                .tracking(0.4)
                .foregroundStyle(t.muted)
        }
        .padding(.horizontal, 22)
        .padding(.bottom, 32)
    }

    private var versionLine: String {
        let build = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.0.0"
        return "fw 0.2.0 · ESP32-S3 · iOS \(build)"
    }
}

/// Navy-tile aperture/crosshair brand mark. Ported from the design's inline
/// SVG in `Screens.jsx:118-124`: central orange core, two orange halo rings,
/// white crosshair blades, and four diagonal ticks. Reproduced using
/// SwiftUI shapes so Dynamic Type + scaling don't rasterize.
struct ApertureGlyph: View {
    let size: CGFloat

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: size * 0.25, style: .continuous)
                .fill(BSL.navy)

            Canvas { ctx, canvasSize in
                let w = canvasSize.width
                let c = CGPoint(x: w / 2, y: w / 2)
                let unit = w / 52  // design was authored at 52×52

                func circle(radius r: CGFloat) -> Path {
                    Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: r * 2, height: r * 2))
                }

                // Outer halo (thin, 0.25 opacity)
                ctx.stroke(circle(radius: 15 * unit),
                           with: .color(BSL.orange.opacity(0.25)),
                           lineWidth: 0.6 * unit)
                // Inner halo (0.5 opacity)
                ctx.stroke(circle(radius: 10 * unit),
                           with: .color(BSL.orange.opacity(0.5)),
                           lineWidth: 1.0 * unit)
                // Core
                ctx.fill(circle(radius: 6 * unit), with: .color(BSL.orange))

                // Crosshair blades
                let blade = StrokeStyle(lineWidth: 1.2 * unit, lineCap: .round)
                var blades = Path()
                blades.move(to: CGPoint(x: c.x, y: c.y - 20 * unit))
                blades.addLine(to: CGPoint(x: c.x, y: c.y - 14 * unit))
                blades.move(to: CGPoint(x: c.x, y: c.y + 14 * unit))
                blades.addLine(to: CGPoint(x: c.x, y: c.y + 20 * unit))
                blades.move(to: CGPoint(x: c.x - 20 * unit, y: c.y))
                blades.addLine(to: CGPoint(x: c.x - 14 * unit, y: c.y))
                blades.move(to: CGPoint(x: c.x + 14 * unit, y: c.y))
                blades.addLine(to: CGPoint(x: c.x + 20 * unit, y: c.y))
                ctx.stroke(blades, with: .color(Color.white.opacity(0.6)), style: blade)

                // Diagonal ticks
                let diag = StrokeStyle(lineWidth: 0.8 * unit, lineCap: .round)
                var diagPath = Path()
                let o1: CGFloat = 14.2 * unit
                let o2: CGFloat = 10.5 * unit
                diagPath.move(to: CGPoint(x: c.x - o1, y: c.y - o1))
                diagPath.addLine(to: CGPoint(x: c.x - o2, y: c.y - o2))
                diagPath.move(to: CGPoint(x: c.x + o2, y: c.y + o2))
                diagPath.addLine(to: CGPoint(x: c.x + o1, y: c.y + o1))
                diagPath.move(to: CGPoint(x: c.x - o1, y: c.y + o1))
                diagPath.addLine(to: CGPoint(x: c.x - o2, y: c.y + o2))
                diagPath.move(to: CGPoint(x: c.x + o2, y: c.y - o2))
                diagPath.addLine(to: CGPoint(x: c.x + o1, y: c.y - o1))
                ctx.stroke(diagPath, with: .color(Color.white.opacity(0.3)), style: diag)
            }
            .frame(width: size * (30.0 / 88.0) * 2, height: size * (30.0 / 88.0) * 2)
        }
        .frame(width: size, height: size)
    }
}
