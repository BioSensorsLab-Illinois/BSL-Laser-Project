import SwiftUI

/// Shared card chrome. Plain rounded rectangle, solid border, no glass / no
/// gradient — matches the Uncodixfy restraint rules under
/// `.agent/skills/Uncodixfy/SKILL.md`.
struct CardShell<Header: View, Content: View>: View {
    let header: Header
    let content: Content

    init(@ViewBuilder header: () -> Header, @ViewBuilder content: () -> Content) {
        self.header = header()
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            header
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.primary)
            content
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(Color(.separator), lineWidth: 1)
        )
    }
}

struct ReadoutRow: View {
    let label: String
    let value: String
    var tone: Tone = .normal
    var stale: Bool = false

    enum Tone { case normal, good, warn, bad }

    var body: some View {
        HStack {
            Text(label)
                .font(.footnote)
                .foregroundStyle(.secondary)
            Spacer()
            Text(stale ? "—" : value)
                .font(.body.monospacedDigit())
                .foregroundStyle(stale ? .secondary : color)
        }
    }

    private var color: Color {
        switch tone {
        case .normal: return .primary
        case .good: return .green
        case .warn: return .orange
        case .bad: return .red
        }
    }
}

struct BlockedReasonChip: View {
    let reason: String
    let tooltip: String

    @State private var showTooltip = false

    var body: some View {
        Button {
            showTooltip = true
        } label: {
            HStack(spacing: 4) {
                Image(systemName: "info.circle")
                Text(reason)
                    .font(.caption.weight(.medium))
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(Color.orange.opacity(0.2))
            .clipShape(Capsule())
            .foregroundStyle(.orange)
        }
        .alert("Blocked", isPresented: $showTooltip) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(tooltip)
        }
    }
}

struct PillIndicator: View {
    let label: String
    let on: Bool
    var onColor: Color = .green

    var body: some View {
        Text(label)
            .font(.caption2.weight(.medium))
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(on ? onColor.opacity(0.2) : Color(.tertiarySystemBackground))
            .foregroundStyle(on ? onColor : .secondary)
            .clipShape(Capsule())
            .overlay(
                Capsule().stroke(
                    on ? onColor.opacity(0.6) : Color(.separator),
                    lineWidth: 1
                )
            )
    }
}
