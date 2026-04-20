import SwiftUI
import BSLProtocol

/// V1 — "Clinical Dashboard". The shipping default. Twin-arc NIR hero,
/// compact visible-LED strip, wavelength + TEC grid, and a USB-PD rail card.
/// Extracted 2026-04-19 so the Main view can route between variations from
/// `LayoutPreference`.
struct LayoutV1Dashboard: View {
    var onOpenWavelength: () -> Void

    var body: some View {
        VStack(spacing: 12) {
            NirHeroCard()
            LedStripCard()
            HStack(spacing: 10) {
                WavelengthMiniCard(onTap: onOpenWavelength)
                TemperatureMiniCard()
            }
            PowerRailCard()
        }
    }
}
