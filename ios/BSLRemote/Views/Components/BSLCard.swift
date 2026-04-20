import SwiftUI

/// Redesign card chrome. Plain surface, 0.5 pt border, 20 pt corner radius.
/// Matches `bsl-laser-system/project/BSL Laser Controller.html:469-480`.
struct BSLCard<Content: View>: View {
    let pad: CGFloat
    let content: Content

    init(pad: CGFloat = 16, @ViewBuilder content: () -> Content) {
        self.pad = pad
        self.content = content()
    }

    @Environment(\.bslTheme) private var t

    var body: some View {
        content
            .padding(pad)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(t.surface)
            .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .strokeBorder(t.border, lineWidth: 0.5)
            )
    }
}

/// Uppercase label + big tabular value pair used across metrics in the
/// redesign.
struct BigMetricRow: View {
    @Environment(\.bslTheme) private var t
    let label: String
    let bigValue: String
    let bigUnit: String
    var setValue: String? = nil
    var setUnit: String = ""

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(label)
                .font(.system(size: 11, weight: .semibold).monospacedDigit())
                .tracking(0.5)
                .foregroundStyle(t.muted)
            Spacer(minLength: 8)
            VStack(alignment: .trailing, spacing: 1) {
                HStack(alignment: .firstTextBaseline, spacing: 3) {
                    Text(bigValue)
                        .font(.system(size: 22, weight: .bold).monospacedDigit())
                        .foregroundStyle(t.ink)
                        .contentTransition(.numericText())
                        .animation(.easeInOut(duration: 0.25), value: bigValue)
                    Text(bigUnit)
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(t.muted)
                }
                if let set = setValue {
                    HStack(spacing: 2) {
                        Text("→ \(set) \(setUnit)")
                            .font(.system(size: 10, weight: .semibold).monospacedDigit())
                            .foregroundStyle(BSL.orange)
                            .contentTransition(.numericText())
                            .animation(.easeInOut(duration: 0.25), value: set)
                    }
                }
            }
        }
    }
}
