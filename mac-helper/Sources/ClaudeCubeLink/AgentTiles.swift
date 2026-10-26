import AppKit
import CubeLinkCore

/// The Agents card as the web board draws it (`bridge/board/board.css`, `.tile`): two columns of square-cornered
/// tiles on the deck's black, each with its critter (`Critter`), name, status line, Claude chip and the context
/// gauge round its edge. While the menu is open the critters, the running dot wave and the hungry stripes move as
/// the cube and the board do (`Critter`, `TileGround`), on the system's uptime so a menu rebuilt by a new payload
/// carries on where it was. A click on a tile calls `onTile`.
final class AgentTilesView: NSView {
    private static let pad: CGFloat = 6, gap: CGFloat = 6, margin: CGFloat = 8
    static let tileH: CGFloat = 120
    private static let cell: CGFloat = 4

    private static func rgb(_ v: UInt32, _ a: CGFloat = 1) -> NSColor {
        NSColor(srgbRed: CGFloat(v >> 16 & 0xFF) / 255, green: CGFloat(v >> 8 & 0xFF) / 255, blue: CGFloat(v & 0xFF) / 255, alpha: a)
    }
    // The deck's palette (root CLAUDE.md, Theme).
    private static let bg = rgb(0x0A0A0A), tile = rgb(0x0F0F0D), ink = rgb(0xE8E4DC), dim = rgb(0x7A776F)
    private static let line = rgb(0x262622), accent = rgb(0xFF4D00), empty = rgb(0x0B0B0B), chip = rgb(0xC98B6B)
    private static let track = rgb(0x2A2925)

    private let tiles: [AgentTile]
    private var hovered: Int?
    private var timer: Timer?
    private static let frameInterval: TimeInterval = 1.0 / 20
    var onTile: (String) -> Void = { _ in }

    init(_ tiles: [AgentTile], width: CGFloat) {
        self.tiles = tiles
        let rows = CGFloat((tiles.count + 1) / 2)
        let h = Self.pad * 2 + rows * Self.tileH + max(0, rows - 1) * Self.gap
        super.init(frame: NSRect(x: 0, y: 0, width: width, height: h))
    }

    required init?(coder: NSCoder) { fatalError() }

    override var isFlipped: Bool { true }

    /// The animation clock: every view reads the same one.
    private static var nowMs: UInt64 { UInt64(ProcessInfo.processInfo.systemUptime * 1000) }

    private var animates: Bool {
        tiles.contains { Critter.animates(pose: $0.pose) || $0.kind == "run" || ($0.hungry && $0.kind != "wait") }
    }

    // In the window only while the menu is open: start the frames then, stop them when it closes. In .common
    // modes, which include the menu's event tracking.
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        timer?.invalidate()
        timer = nil
        guard window != nil, animates else { return }
        let t = Timer(timeInterval: Self.frameInterval, repeats: true) { [weak self] _ in self?.needsDisplay = true }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    private func tileRect(_ i: Int) -> NSRect {
        let inner = bounds.width - Self.margin * 2 - Self.pad * 2
        let w = ((inner - Self.gap) / 2).rounded(.down)
        let col = CGFloat(i % 2), row = CGFloat(i / 2)
        return NSRect(x: Self.margin + Self.pad + col * (w + Self.gap), y: Self.pad + row * (Self.tileH + Self.gap),
                      width: w, height: Self.tileH)
    }

    private func tileAt(_ p: NSPoint) -> Int? { tiles.indices.first { tileRect($0).contains(p) } }

    // MARK: - mouse

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeAlways],
                                       owner: self))
    }

    override func mouseMoved(with e: NSEvent) { setHovered(tileAt(convert(e.locationInWindow, from: nil))) }
    override func mouseExited(with e: NSEvent) { setHovered(nil) }
    override func mouseDown(with e: NSEvent) {}  // the click lands on mouseUp, as a menu item's does
    override func mouseUp(with e: NSEvent) {
        guard let i = tileAt(convert(e.locationInWindow, from: nil)) else { return }
        enclosingMenuItem?.menu?.cancelTracking()
        onTile(tiles[i].id)
    }

    private func setHovered(_ i: Int?) {
        guard i != hovered else { return }
        hovered = i
        needsDisplay = true
    }

    // MARK: - drawing

    private struct Look {
        var ground = AgentTilesView.tile, body = AgentTilesView.ink, label = AgentTilesView.ink, meta = AgentTilesView.dim
        var chip = AgentTilesView.chip, ring = AgentTilesView.accent, track = AgentTilesView.track
        var border: NSColor?
    }

    /// The board's per-state colours (`.tile.st-*`, `.tile.hungry`).
    private static func look(_ t: AgentTile) -> Look {
        var l = Look()
        switch t.kind {
        case "wait":
            l.ground = accent; l.body = bg; l.label = bg; l.meta = bg; l.chip = bg; l.ring = bg
            l.track = bg.withAlphaComponent(0.25)
        case "run": l.body = accent
        case "err": l.body = ink; l.meta = accent
        case "exit": l.body = dim; l.label = dim; l.ground = empty; l.border = line
        case "idle", "start": l.body = dim; l.label = dim
        default: break
        }
        if t.hungry && t.kind != "wait" { l.body = accent; l.meta = accent; l.label = ink }
        return l
    }

    override func draw(_ dirty: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        Self.bg.setFill()
        NSRect(x: Self.margin, y: 0, width: bounds.width - Self.margin * 2, height: bounds.height).fill()
        let ms = Self.nowMs
        for (i, t) in tiles.enumerated() { drawTile(t, in: tileRect(i), hovered: i == hovered, ms: ms, ctx) }
    }

    private func drawTile(_ t: AgentTile, in r: NSRect, hovered: Bool, ms: UInt64, _ ctx: CGContext) {
        let l = Self.look(t)
        l.ground.setFill()
        r.fill()
        ctx.saveGState()
        ctx.clip(to: r)
        if t.hungry && t.kind != "wait" { drawStripes(r, ms: ms, ctx) } else if t.kind == "run" { drawRunGround(r, ms: ms) }
        ctx.restoreGState()
        if let b = l.border { b.setStroke(); NSBezierPath(rect: r.insetBy(dx: 0.5, dy: 0.5)).stroke() }
        if hovered {
            (t.kind == "wait" ? Self.ink : Self.dim).setStroke()
            NSBezierPath(rect: r.insetBy(dx: 3.5, dy: 3.5)).stroke()
        }
        drawRing(r, fraction: CGFloat(t.context) / 100, l, ctx)

        l.chip.setFill()
        NSRect(x: r.maxX - 15, y: r.minY + 8, width: 7, height: 7).fill()
        if let s = t.subagents {
            l.body.setFill()
            NSRect(x: r.minX + 9, y: r.minY + 10, width: 9, height: 4).fill()
            if !s.isEmpty { drawText(s, size: 10, color: l.meta, at: NSPoint(x: r.minX + 21, y: r.minY + 5), maxW: 40, centered: false) }
        }

        // The critter: the board centres its 18-cell viewBox; the whole region keeps hats that reach past it.
        let c = Self.cell
        let origin = NSPoint(x: (r.midX - 9 * c).rounded(), y: r.minY + 20 + 4 * c)
        drawCritter(t, at: origin, look: l, ms: ms)

        drawText(t.name, size: 12, weight: .medium, color: l.label, at: NSPoint(x: r.minX + 8, y: r.minY + Self.textBand.lowerBound + 4),
                 maxW: r.width - 16)
        drawText(t.line, size: 11, color: l.meta, at: NSPoint(x: r.minX + 8, y: r.minY + Self.textBand.lowerBound + 22), maxW: r.width - 16)
    }

    private func drawCritter(_ t: AgentTile, at o: NSPoint, look l: Look, ms: UInt64) {
        let c = Self.cell
        for r in Critter.shapes(pose: t.pose, hat: t.hat, ms: ms) {
            let base: NSColor = switch r.paint {
            case .body: l.body
            case .cut: l.ground
            case .flag: Self.ink
            case .accent: Self.accent
            case .rgb(let v): Self.rgb(v)
            }
            base.withAlphaComponent(base.alphaComponent * r.alpha).setFill()
            NSRect(x: o.x + r.x * c, y: o.y + r.y * c, width: r.w * c, height: r.h * c).fill(using: .sourceOver)
        }
    }

    /// The context gauge: a 2 px track round the edge, filled clockwise from the top centre.
    private func drawRing(_ r: NSRect, fraction: CGFloat, _ l: Look, _ ctx: CGContext) {
        let e = r.insetBy(dx: 1, dy: 1)
        let path = CGMutablePath()
        path.move(to: CGPoint(x: e.midX, y: e.minY))
        path.addLine(to: CGPoint(x: e.maxX, y: e.minY))
        path.addLine(to: CGPoint(x: e.maxX, y: e.maxY))
        path.addLine(to: CGPoint(x: e.minX, y: e.maxY))
        path.addLine(to: CGPoint(x: e.minX, y: e.minY))
        path.closeSubpath()
        let length = 2 * (e.width + e.height)
        ctx.saveGState()
        ctx.setLineWidth(2)
        ctx.setLineJoin(.miter)
        ctx.addPath(path)
        ctx.setStrokeColor(l.track.cgColor)
        ctx.strokePath()
        if fraction > 0 {
            ctx.addPath(path)
            ctx.setStrokeColor(l.ring.cgColor)
            ctx.setLineDash(phase: 0, lengths: [length * min(fraction, 1), length])
            ctx.strokePath()
        }
        ctx.restoreGState()
    }

    /// The name and status line's band, in tile coordinates: the ground stops behind it, as on the cube.
    private static let textBand: ClosedRange<CGFloat> = 72...112

    /// Running: agents-deck's dot matrix, faint accent dots that a diagonal wave brightens, and four rising specks.
    private func drawRunGround(_ r: NSRect, ms: UInt64) {
        var y = TileGround.dotInset
        while y < r.height - 2 {
            if Self.textBand.contains(y) { y += TileGround.dotPitch; continue }
            var x = TileGround.dotInset
            while x < r.width - 2 {
                Self.accent.withAlphaComponent(TileGround.dotAlpha(dx: x, dy: y, w: r.width, h: r.height, ms: ms)).setFill()
                NSRect(x: r.minX + x, y: r.minY + y, width: 1.5, height: 1.5).fill(using: .sourceOver)
                x += TileGround.dotPitch
            }
            y += TileGround.dotPitch
        }
        for i in 0..<4 {
            let p = TileGround.particle(i, w: r.width, h: r.height, ms: ms)
            guard p.alpha > 0, !Self.textBand.contains(p.y), !Self.textBand.contains(p.y + TileGround.particleSize) else { continue }
            Self.accent.withAlphaComponent(p.alpha).setFill()
            NSRect(x: r.minX + p.x, y: r.minY + p.y, width: TileGround.particleSize, height: TileGround.particleSize)
                .fill(using: .sourceOver)
        }
    }

    /// Hungry: the board's 135° accent stripes, 12 px on 24, sliding.
    private func drawStripes(_ r: NSRect, ms: UInt64, _ ctx: CGContext) {
        ctx.setStrokeColor(Self.accent.withAlphaComponent(0.16).cgColor)
        ctx.setLineWidth(12)
        let step: CGFloat = 24 * 2.squareRoot()
        var x = r.minX - r.height - step + TileGround.stripeOffset(ms: ms)
        while x < r.maxX + r.height {
            ctx.move(to: CGPoint(x: x, y: r.minY))
            ctx.addLine(to: CGPoint(x: x + r.height, y: r.maxY))
            x += step
        }
        ctx.strokePath()
    }

    private func drawText(_ s: String, size: CGFloat, weight: NSFont.Weight = .regular, color: NSColor, at p: NSPoint,
                          maxW: CGFloat, centered: Bool = true) {
        let style = NSMutableParagraphStyle()
        style.alignment = centered ? .center : .left
        style.lineBreakMode = .byTruncatingTail
        let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.monospacedSystemFont(ofSize: size, weight: weight),
                                                    .foregroundColor: color, .paragraphStyle: style]
        (s as NSString).draw(with: NSRect(x: p.x, y: p.y, width: maxW, height: size + 6),
                             options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine], attributes: attrs)
    }
}
