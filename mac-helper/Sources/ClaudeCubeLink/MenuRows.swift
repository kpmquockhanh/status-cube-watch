import AppKit
import CubeLinkCore

let menuRowWidth: CGFloat = 320  // two Agents tiles side by side
private let inset: CGFloat = 16

/// Orange (the accent) #FF4D00 is the deck palette's. Ink is the system text colour and yellow is #E8B94A in a dark
/// appearance and a darker amber (#B07D12) in a light one, since the palette's pale tones would vanish on a light menu.
func tintColor(_ t: BarTint) -> NSColor {
    switch t {
    case .ink: .labelColor
    case .yellow:
        NSColor(name: nil) { appearance in
            let dark = appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            return dark ? NSColor(srgbRed: 0xE8 / 255, green: 0xB9 / 255, blue: 0x4A / 255, alpha: 1)
                        : NSColor(srgbRed: 0xB0 / 255, green: 0x7D / 255, blue: 0x12 / 255, alpha: 1)
        }
    case .orange: NSColor(srgbRed: 0xFF / 255, green: 0x4D / 255, blue: 0x00 / 255, alpha: 1)
    }
}

private func text(_ s: String, size: CGFloat, weight: NSFont.Weight = .regular, color: NSColor = .labelColor,
                  mono: Bool = false) -> NSTextField {
    let f = NSTextField(labelWithString: s)
    f.font = mono ? .monospacedDigitSystemFont(ofSize: size, weight: weight) : .systemFont(ofSize: size, weight: weight)
    f.textColor = color
    f.sizeToFit()
    return f
}

/// A gauge row: label left, value right, a thin tinted bar, then a caption ("2h 28m to reset").
final class UsageRowView: NSView {
    final class Bar: NSView {
        var fraction = 0.0
        var color = NSColor.systemGreen
        override func draw(_ dirty: NSRect) {
            NSColor.labelColor.withAlphaComponent(0.12).setFill()
            bounds.fill()
            guard fraction > 0 else { return }
            color.setFill()
            NSRect(x: 0, y: 0, width: max(2, bounds.width * fraction), height: bounds.height).fill()
        }
    }

    init(_ row: CardRow) {
        let hasBar = row.fraction != nil
        let height: CGFloat = hasBar ? 54 : 26
        super.init(frame: NSRect(x: 0, y: 0, width: menuRowWidth, height: height))

        let label = text(row.label, size: 13, weight: .semibold)
        label.setFrameOrigin(NSPoint(x: inset, y: height - label.frame.height - 6))
        addSubview(label)

        if !row.value.isEmpty {
            let value = text(row.value, size: 13, weight: .semibold, color: hasBar ? tintColor(row.tint) : .secondaryLabelColor,
                             mono: true)
            value.setFrameOrigin(NSPoint(x: menuRowWidth - inset - value.frame.width, y: label.frame.minY))
            addSubview(value)
        }
        guard let fraction = row.fraction else { return }

        let bar = Bar(frame: NSRect(x: inset, y: label.frame.minY - 10, width: menuRowWidth - inset * 2, height: 5))
        bar.fraction = fraction
        bar.color = tintColor(row.tint)
        addSubview(bar)

        if !row.detail.isEmpty {
            let caption = text(row.detail, size: 11, color: .secondaryLabelColor)
            caption.setFrameOrigin(NSPoint(x: inset, y: bar.frame.minY - caption.frame.height - 3))
            addSubview(caption)
        }
    }

    required init?(coder: NSCoder) { fatalError() }
}

/// One usage window on one line: "5H 38%", a bar, and the countdown ("1h 58m") at the right.
final class CompactUsageRowView: NSView {
    /// Fits rows whose countdown is short; a longer caption (the extra cards' "$1.20 · today") keeps `UsageRowView`.
    static func fits(_ row: CardRow) -> Bool { row.fraction != nil && row.detail.count <= 8 }

    init(_ row: CardRow) {
        super.init(frame: NSRect(x: 0, y: 0, width: menuRowWidth, height: 22))
        let label = text(row.label, size: 13, weight: .semibold)
        label.setFrameOrigin(NSPoint(x: inset, y: (22 - label.frame.height) / 2))
        addSubview(label)
        let value = text(row.value, size: 13, weight: .semibold, color: tintColor(row.tint), mono: true)
        value.setFrameOrigin(NSPoint(x: label.frame.maxX + 5, y: label.frame.minY))
        addSubview(value)

        let detailW: CGFloat = 52
        let caption = text(row.detail, size: 11, color: .secondaryLabelColor, mono: true)
        caption.alignment = .right
        caption.frame = NSRect(x: menuRowWidth - inset - detailW, y: (22 - caption.frame.height) / 2, width: detailW,
                               height: caption.frame.height)
        addSubview(caption)

        let x0 = max(inset + 72, value.frame.maxX + 10)
        let bar = UsageRowView.Bar(frame: NSRect(x: x0, y: 9, width: caption.frame.minX - 10 - x0, height: 4))
        bar.fraction = row.fraction ?? 0
        bar.color = tintColor(row.tint)
        addSubview(bar)
    }

    required init?(coder: NSCoder) { fatalError() }
}

/// "Agents" with the bridge's caption ("1 needs you") at the right.
final class HeadingRowView: NSView {
    init(_ title: String, detail: String) {
        super.init(frame: NSRect(x: 0, y: 0, width: menuRowWidth, height: 26))
        let t = text(title, size: 13, weight: .semibold)
        t.setFrameOrigin(NSPoint(x: inset, y: 4))
        addSubview(t)
        let d = text(detail, size: 12, color: .secondaryLabelColor)
        d.setFrameOrigin(NSPoint(x: menuRowWidth - inset - d.frame.width, y: 5))
        addSubview(d)
    }

    required init?(coder: NSCoder) { fatalError() }
}

/// "● Bridge: running" with a coloured dot.
final class StatusRowView: NSView {
    init(_ s: String, dot: NSColor) {
        super.init(frame: NSRect(x: 0, y: 0, width: menuRowWidth, height: 22))
        let label = text(s, size: 12, color: .secondaryLabelColor)
        label.setFrameOrigin(NSPoint(x: inset + 16, y: (22 - label.frame.height) / 2))
        addSubview(label)
        let d = NSView(frame: NSRect(x: inset + 2, y: 8, width: 7, height: 7))
        d.wantsLayer = true
        d.layer?.cornerRadius = 3.5
        d.layer?.backgroundColor = dot.cgColor
        addSubview(d)
    }

    required init?(coder: NSCoder) { fatalError() }
}
