import SwiftUI
import UIKit

/// The app's look: the cube's own palette on a near-black ground, so the phone and the
/// handlebar read as one product. Dark only (`CubeNavApp` forces it).
enum Theme {
    static let bg = Color(hex: 0x0B0D12)
    static let surface = Color(hex: 0x151922)
    static let surfaceHigh = Color(hex: 0x1E2430)
    static let hairline = Color.white.opacity(0.08)
    static let ink = Color(hex: 0xE7ECF5)
    static let dim = Color(hex: 0x98A1B3)       // 7:1 on bg
    static let accent = Color(hex: 0xFF8A5B)    // the cube's accent
    static let route = Color(hex: 0x4DA3FF)     // the cube's route blue
    static let good = Color(hex: 0x46D08A)
    static let warn = Color(hex: 0xF5C451)
    static let bad = Color(hex: 0xFF6B6B)

    static let radius: CGFloat = 22
    static let gutter: CGFloat = 16

    /// Big glanceable numbers: rounded, tabular so they don't jitter while counting.
    static func number(_ size: CGFloat, _ weight: Font.Weight = .bold) -> Font {
        .system(size: size, weight: weight, design: .rounded).monospacedDigit()
    }
}

extension Color {
    init(hex: UInt32) {
        self.init(red: Double((hex >> 16) & 0xFF) / 255, green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255)
    }
}

/// A raised panel: the one container shape used across the app.
struct Panel<Content: View>: View {
    var padding: CGFloat = 16
    @ViewBuilder var content: Content
    var body: some View {
        content
            .padding(padding)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.surface, in: RoundedRectangle(cornerRadius: Theme.radius, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: Theme.radius, style: .continuous).strokeBorder(Theme.hairline))
    }
}

/// The single primary action on a screen: full width, 56 pt, accent fill, a light haptic on press.
struct PrimaryButtonStyle: ButtonStyle {
    var tint: Color = Theme.accent
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.headline)
            .foregroundStyle(Color.black)
            .frame(maxWidth: .infinity, minHeight: 56)
            .background(tint, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            .opacity(configuration.isPressed ? 0.85 : 1)
            .scaleEffect(configuration.isPressed ? 0.98 : 1)
            .animation(.spring(response: 0.25, dampingFraction: 0.8), value: configuration.isPressed)
            .sensoryFeedback(.impact(weight: .light), trigger: configuration.isPressed) { _, pressed in pressed }
    }
}

/// Secondary actions: same height, quiet surface.
struct SecondaryButtonStyle: ButtonStyle {
    var tint: Color = Theme.ink
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.headline)
            .foregroundStyle(tint)
            .frame(maxWidth: .infinity, minHeight: 52)
            .background(Theme.surfaceHigh, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .opacity(configuration.isPressed ? 0.7 : 1)
            .animation(.easeOut(duration: 0.15), value: configuration.isPressed)
    }
}

/// A small round icon button (settings, close): 44 pt hit area.
struct RoundIconButton: View {
    let systemName: String
    let label: String
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.system(size: 17, weight: .semibold))
                .foregroundStyle(Theme.ink)
                .frame(width: 44, height: 44)
                .background(.ultraThinMaterial, in: Circle())
                .overlay(Circle().strokeBorder(Theme.hairline))
        }
        .accessibilityLabel(label)
    }
}

/// A compact status pill: coloured dot + short text.
struct StatusPill: View {
    let text: String
    let color: Color
    var systemImage: String?
    var body: some View {
        HStack(spacing: 6) {
            if let systemImage {
                Image(systemName: systemImage).font(.caption.weight(.bold)).foregroundStyle(color)
            } else {
                Circle().fill(color).frame(width: 8, height: 8)
            }
            Text(text).font(.footnote.weight(.semibold)).foregroundStyle(Theme.ink)
        }
        .padding(.horizontal, 12).padding(.vertical, 7)
        .background(.ultraThinMaterial, in: Capsule())
        .overlay(Capsule().strokeBorder(Theme.hairline))
        .accessibilityElement(children: .combine)
    }
}

/// A small uppercase section label.
struct SectionLabel: View {
    let text: String
    init(_ text: String) { self.text = text }
    var body: some View {
        Text(text.uppercased())
            .font(.caption.weight(.semibold))
            .tracking(1.2)
            .foregroundStyle(Theme.dim)
            .accessibilityAddTraits(.isHeader)
    }
}

enum Haptics {
    static func success() { UINotificationFeedbackGenerator().notificationOccurred(.success) }
}
