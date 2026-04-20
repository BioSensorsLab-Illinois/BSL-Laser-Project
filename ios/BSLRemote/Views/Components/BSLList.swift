import SwiftUI

/// Inset-grouped list primitives matching the design bundle's Screens.jsx /
/// SettingsStack.jsx. Deliberately not using `SwiftUI.Form` because the
/// design's card geometry (14 pt radius, 0.5 pt border, hairline separators,
/// 28 pt rounded icon tiles on certain rows) does not reproduce on stock
/// iOS `List` without fighting the platform.

// MARK: - Group

/// A single inset-grouped section with optional header label + footer caption.
struct BSLListGroup<Content: View>: View {
    var label: String? = nil
    var footer: String? = nil
    @ViewBuilder var content: () -> Content

    @Environment(\.bslTheme) private var t

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if let label {
                Text(label.uppercased())
                    .font(.system(size: 11, weight: .semibold))
                    .tracking(0.6)
                    .foregroundStyle(t.muted)
                    .padding(.horizontal, 16)
                    .padding(.bottom, 8)
            }
            VStack(spacing: 0) { content() }
                .background(t.rowFill)
                .overlay(
                    RoundedRectangle(cornerRadius: 14, style: .continuous)
                        .strokeBorder(t.border, lineWidth: 0.5)
                )
                .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            if let footer {
                Text(footer)
                    .font(.system(size: 11))
                    .foregroundStyle(t.muted)
                    .lineSpacing(1.5)
                    .padding(.top, 8)
                    .padding(.horizontal, 16)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(.bottom, 20)
    }
}

// MARK: - Row

/// Horizontal row in a list group. Mirrors `SettingsStack.jsx::ListRow`:
///   - optional 28 pt rounded icon tile on the left (tone: navy / brand / subtle)
///   - label + optional sub-label stack
///   - optional trailing "right" view (text, value, toggle, etc.)
///   - chevron when `action != nil`
///   - hairline bottom separator unless `isLast == true`
struct BSLListRow<Right: View>: View {
    var icon: Image? = nil
    var iconTone: IconTone = .subtle
    var iconRendering: IconRendering = .template
    var label: String
    var sublabel: String? = nil
    var destructive: Bool = false
    var isLast: Bool = false
    var action: (() -> Void)? = nil
    @ViewBuilder var right: () -> Right

    enum IconTone { case subtle, navy, brand, nir, warn }
    enum IconRendering { case template, original }

    @Environment(\.bslTheme) private var t

    var body: some View {
        let row = HStack(spacing: 12) {
            if let icon {
                iconView(icon)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(label)
                    .font(.system(size: 14, weight: .medium))
                    .foregroundStyle(destructive ? BSL.warn : t.ink)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .multilineTextAlignment(.leading)
                if let sublabel {
                    Text(sublabel)
                        .font(.system(size: 11))
                        .foregroundStyle(t.muted)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .multilineTextAlignment(.leading)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            right()
            if action != nil {
                Image(systemName: "chevron.right")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(t.dim)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
        .contentShape(Rectangle())
        .overlay(alignment: .bottom) {
            if !isLast {
                Rectangle().fill(t.border).frame(height: 0.5).padding(.leading, icon == nil ? 14 : 54)
            }
        }

        if let action {
            Button(action: action) { row }
                .buttonStyle(BSLRowButtonStyle())
        } else {
            row
        }
    }

    @ViewBuilder
    private func iconView(_ image: Image) -> some View {
        let bg: Color = {
            switch iconTone {
            case .subtle: return t.dark ? Color.white.opacity(0.08) : Color(red: 0.945, green: 0.952, blue: 0.972)
            case .navy:   return BSL.navy
            case .brand:  return BSL.orange
            case .nir:    return BSL.nir
            case .warn:   return BSL.warn
            }
        }()
        let fg: Color = {
            switch iconTone {
            case .subtle: return t.muted
            default:      return .white
            }
        }()
        let rendered = iconRendering == .original
            ? image.renderingMode(.original)
            : image.renderingMode(.template)
        ZStack {
            RoundedRectangle(cornerRadius: 7, style: .continuous).fill(bg)
            rendered
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(fg)
        }
        .frame(width: 28, height: 28)
    }
}

extension BSLListRow where Right == EmptyView {
    init(
        icon: Image? = nil,
        iconTone: IconTone = .subtle,
        iconRendering: IconRendering = .template,
        label: String,
        sublabel: String? = nil,
        destructive: Bool = false,
        isLast: Bool = false,
        action: (() -> Void)? = nil
    ) {
        self.icon = icon
        self.iconTone = iconTone
        self.iconRendering = iconRendering
        self.label = label
        self.sublabel = sublabel
        self.destructive = destructive
        self.isLast = isLast
        self.action = action
        self.right = { EmptyView() }
    }
}

/// A read-only value row — label on the left, monospaced-digit value on the
/// right, no tap target. Shortcut for the Context section.
struct BSLValueRow: View {
    var label: String
    var value: String
    var valueColor: Color? = nil
    var isLast: Bool = false

    @Environment(\.bslTheme) private var t

    var body: some View {
        BSLListRow(label: label, isLast: isLast) {
            Text(value)
                .font(.system(size: 13, weight: .semibold).monospacedDigit())
                .foregroundStyle(valueColor ?? t.muted)
        }
    }
}

/// Pressed-state background for a ListRow. Fades the row tint slightly so
/// taps have a visible response on both light and dark.
private struct BSLRowButtonStyle: ButtonStyle {
    @Environment(\.bslTheme) private var t
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed
                        ? (t.dark ? Color.white.opacity(0.04) : Color(red: 0.96, green: 0.965, blue: 0.980))
                        : Color.clear)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}

// MARK: - Toggle

/// iOS-style green toggle matching the design. Larger than stock, uses
/// `BSL.ok` for the on-track and includes a soft shadow on the thumb.
struct BSLToggle: View {
    @Binding var isOn: Bool
    var disabled: Bool = false

    var body: some View {
        Button {
            if !disabled { isOn.toggle() }
        } label: {
            ZStack(alignment: isOn ? .trailing : .leading) {
                Capsule()
                    .fill(isOn ? BSL.ok : Color(red: 0.47, green: 0.47, blue: 0.502, opacity: 0.30))
                Circle()
                    .fill(Color.white)
                    .shadow(color: .black.opacity(0.18), radius: 1.5, x: 0, y: 1)
                    .padding(2)
            }
            .frame(width: 42, height: 26)
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.4 : 1.0)
        .animation(.easeInOut(duration: 0.18), value: isOn)
        .accessibilityAddTraits(.isButton)
        .accessibilityValue(isOn ? "on" : "off")
    }
}

// MARK: - NavBar

/// Custom top nav bar matching the design. Back (orange chevron + "Back"
/// text) or Cancel on the left, centered title, and a right action slot
/// (typically "Apply", "Done", or "Lock"). Pinned below the status bar via
/// the parent `safeAreaInset`/`ignoresSafeArea` geometry.
struct BSLNavBar<Right: View>: View {
    var title: String
    var backLabel: String? = "Back"
    var onBack: (() -> Void)? = nil
    @ViewBuilder var right: () -> Right

    @Environment(\.bslTheme) private var t

    var body: some View {
        HStack(spacing: 8) {
            leadingSlot
                .frame(width: 72, alignment: .leading)
            Text(title)
                .font(.system(size: 15, weight: .semibold))
                .foregroundStyle(t.ink)
                .frame(maxWidth: .infinity)
                .lineLimit(1)
            HStack { right() }
                .frame(width: 72, alignment: .trailing)
        }
        .padding(.horizontal, 14)
        .padding(.top, 12)
        .padding(.bottom, 10)
        .background(t.bg)
        .overlay(
            Rectangle().fill(t.border).frame(height: 0.5), alignment: .bottom
        )
    }

    @ViewBuilder private var leadingSlot: some View {
        if let onBack {
            Button(action: onBack) {
                HStack(spacing: 2) {
                    Image(systemName: "chevron.left")
                        .font(.system(size: 15, weight: .semibold))
                    if let backLabel {
                        Text(backLabel)
                            .font(.system(size: 15))
                    }
                }
                .foregroundStyle(BSL.orange)
                .padding(.vertical, 4)
                .padding(.trailing, 4)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        } else {
            Color.clear.frame(width: 1, height: 1)
        }
    }
}

extension BSLNavBar where Right == EmptyView {
    init(title: String, backLabel: String? = "Back", onBack: (() -> Void)? = nil) {
        self.title = title
        self.backLabel = backLabel
        self.onBack = onBack
        self.right = { EmptyView() }
    }
}

// MARK: - Buttons

/// Primary orange button. Full-width by default. Matches the design's
/// rounded-16 orange fill with a soft drop-shadow.
struct BSLPrimaryButton: View {
    var title: String
    var icon: Image? = nil
    var tint: Color = BSL.orange
    var disabled: Bool = false
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 8) {
                if let icon {
                    icon
                        .renderingMode(.template)
                        .font(.system(size: 15, weight: .semibold))
                }
                Text(title)
                    .font(.system(size: 15, weight: .bold))
                    .tracking(0.3)
            }
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 14)
            .background(tint)
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            .shadow(color: tint.opacity(disabled ? 0 : 0.30), radius: 10, x: 0, y: 6)
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.5 : 1.0)
    }
}

/// Outlined secondary button — thin border, inline icon + label.
struct BSLSecondaryButton: View {
    var title: String
    var icon: Image? = nil
    var disabled: Bool = false
    var action: () -> Void

    @Environment(\.bslTheme) private var t

    var body: some View {
        Button(action: action) {
            HStack(spacing: 8) {
                if let icon {
                    icon
                        .renderingMode(.template)
                        .font(.system(size: 14, weight: .semibold))
                }
                Text(title)
                    .font(.system(size: 14, weight: .semibold))
            }
            .foregroundStyle(t.ink)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 13)
            .background(t.rowFill)
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .strokeBorder(t.border, lineWidth: 0.5)
            )
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.5 : 1.0)
    }
}

/// Outlined tertiary button — tinted text + thin border; used for "Connect
/// to URL" inside the Manual disclosure.
struct BSLTertiaryButton: View {
    var title: String
    var tint: Color = BSL.orange
    var disabled: Bool = false
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 12.5, weight: .semibold))
                .foregroundStyle(tint)
                .padding(.vertical, 9)
                .padding(.horizontal, 14)
                .overlay(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .strokeBorder(tint, lineWidth: 0.5)
                )
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.4 : 1.0)
    }
}

// MARK: - Confirm dialog

/// Modal confirm dialog used by the Service Mode enter/exit flow.
/// Presented via `.overlay` on the host view so it covers only that screen.
struct BSLConfirmDialog: View {
    var title: String
    var message: String
    var confirmLabel: String
    var confirmTint: Color = BSL.orange
    var onConfirm: () -> Void
    var onCancel: () -> Void

    @Environment(\.bslTheme) private var t

    var body: some View {
        ZStack {
            Color.black.opacity(0.5)
                .ignoresSafeArea()
                .onTapGesture(perform: onCancel)
            VStack(spacing: 14) {
                VStack(spacing: 8) {
                    Text(title)
                        .font(.system(size: 15, weight: .bold))
                        .foregroundStyle(t.ink)
                        .multilineTextAlignment(.center)
                    Text(message)
                        .font(.system(size: 12))
                        .foregroundStyle(t.muted)
                        .multilineTextAlignment(.center)
                        .fixedSize(horizontal: false, vertical: true)
                        .lineSpacing(1.5)
                }
                HStack(spacing: 8) {
                    Button(action: onCancel) {
                        Text("Cancel")
                            .font(.system(size: 13, weight: .medium))
                            .foregroundStyle(t.ink)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 11)
                            .overlay(
                                RoundedRectangle(cornerRadius: 10, style: .continuous)
                                    .strokeBorder(t.border, lineWidth: 0.5)
                            )
                    }
                    .buttonStyle(.plain)
                    Button(action: onConfirm) {
                        Text(confirmLabel)
                            .font(.system(size: 13, weight: .bold))
                            .foregroundStyle(.white)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 11)
                            .background(confirmTint)
                            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(20)
            .frame(maxWidth: 320)
            .background(t.surface)
            .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .strokeBorder(t.border, lineWidth: 0.5)
            )
            .padding(.horizontal, 24)
        }
    }
}
