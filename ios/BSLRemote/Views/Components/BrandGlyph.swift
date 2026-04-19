import SwiftUI

/// Brand mark — navy rounded square + crosshair aperture blades + orange beam
/// core. Drawn with SwiftUI primitives so it scales cleanly. Mirrors the SVG
/// in `bsl-laser-system/project/BSL Laser Controller.html:647-666`.
struct BrandGlyph: View {
    var size: CGFloat = 32

    var body: some View {
        RoundedRectangle(cornerRadius: size * 0.3125, style: .continuous)
            .fill(BSL.navy)
            .frame(width: size, height: size)
            .overlay(
                Canvas { ctx, size in
                    let c = CGPoint(x: size.width / 2, y: size.height / 2)
                    let r = size.width * 0.4
                    // outer ring
                    ctx.stroke(
                        Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: r * 2, height: r * 2)),
                        with: .color(.white.opacity(0.35)),
                        lineWidth: 1
                    )
                    // aperture blades (N/S/E/W)
                    let blade = r * 0.44
                    let gap = r * 0.3
                    var blades = Path()
                    blades.move(to: CGPoint(x: c.x, y: c.y - r * 0.94)); blades.addLine(to: CGPoint(x: c.x, y: c.y - gap))
                    blades.move(to: CGPoint(x: c.x, y: c.y + gap));       blades.addLine(to: CGPoint(x: c.x, y: c.y + r * 0.94))
                    blades.move(to: CGPoint(x: c.x - r * 0.94, y: c.y)); blades.addLine(to: CGPoint(x: c.x - gap, y: c.y))
                    blades.move(to: CGPoint(x: c.x + gap, y: c.y));       blades.addLine(to: CGPoint(x: c.x + r * 0.94, y: c.y))
                    _ = blade
                    ctx.stroke(blades, with: .color(.white.opacity(0.55)), lineWidth: 1)
                    // soft halo
                    let halo = r * 0.56
                    ctx.stroke(
                        Path(ellipseIn: CGRect(x: c.x - halo, y: c.y - halo, width: halo * 2, height: halo * 2)),
                        with: .color(BSL.orange.opacity(0.4)),
                        lineWidth: 0.6
                    )
                    // beam core
                    let core = r * 0.31
                    ctx.fill(
                        Path(ellipseIn: CGRect(x: c.x - core, y: c.y - core, width: core * 2, height: core * 2)),
                        with: .color(BSL.orange)
                    )
                }
            )
            .shadow(color: BSL.navy.opacity(0.18), radius: 3, x: 0, y: 2)
    }
}

#Preview { BrandGlyph(size: 48).padding() }
