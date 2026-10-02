import AppKit
import CubeLinkCore

let menuRowWidth: CGFloat = 264
private let inset: CGFloat = 16

func tintColor(_ t: BarTint) -> NSColor {
    switch t {
    case .normal: .systemGreen
    case .amber: .systemOrange
    case .red: .systemRed
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
    private final class Bar: NSView {
        var fraction = 0.0
        var color = NSColor.systemGreen
        override func draw(_ dirty: NSRect) {
            let r = bounds.height / 2
            NSColor.labelColor.withAlphaComponent(0.12).setFill()
            NSBezierPath(roundedRect: bounds, xRadius: r, yRadius: r).fill()
            guard fraction > 0 else { return }
            color.setFill()
            let w = max(bounds.height, bounds.width * fraction)
            NSBezierPath(roundedRect: NSRect(x: 0, y: 0, width: w, height: bounds.height), xRadius: r, yRadius: r).fill()
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

/// Small uppercase section caption.
func sectionHeader(_ s: String) -> NSView {
    let v = NSView(frame: NSRect(x: 0, y: 0, width: menuRowWidth, height: 22))
    let t = text(s.uppercased(), size: 10, weight: .semibold, color: .tertiaryLabelColor)
    t.setFrameOrigin(NSPoint(x: inset, y: 4))
    v.addSubview(t)
    return v
}
