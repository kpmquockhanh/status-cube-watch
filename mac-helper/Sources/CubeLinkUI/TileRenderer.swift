import AppKit
import CubeLinkCore

/// Draws one agent tile as the web board does (`bridge/board/board.css`, `.tile`): square corners on the deck's
/// black, the critter (`Critter`) in its pose and hat, name, status line, Claude chip and the context gauge round
/// the edge; the running dot wave and the hungry stripes (`TileGround`). Shared by the menu bar's grid
/// (`AgentTilesView`) and the screen saver.
public enum TileRenderer {
    public static let tileH: CGFloat = 120
    private static let cell: CGFloat = 4

    private static func rgb(_ v: UInt32, _ a: CGFloat = 1) -> NSColor {
        NSColor(srgbRed: CGFloat(v >> 16 & 0xFF) / 255, green: CGFloat(v >> 8 & 0xFF) / 255, blue: CGFloat(v & 0xFF) / 255, alpha: a)
    }
    // The deck's palette (root CLAUDE.md, Theme).
    public static let bg = rgb(0x0A0A0A), tile = rgb(0x0F0F0D), ink = rgb(0xE8E4DC), dim = rgb(0x7A776F)
    public static let line = rgb(0x262622), accent = rgb(0xFF4D00), empty = rgb(0x0B0B0B), chip = rgb(0xC98B6B)
    private static let track = rgb(0x2A2925)

    /// The animation clock: every view reads the same one, so a view rebuilt by a new payload carries on.
    public static var nowMs: UInt64 { UInt64(ProcessInfo.processInfo.systemUptime * 1000) }

    /// Whether any of these tiles moves.
    public static func animates(_ tiles: [AgentTile]) -> Bool {
        tiles.contains { Critter.animates(pose: $0.pose) || $0.kind == "run" || ($0.hungry && $0.kind != "wait") }
    }

    struct Look {
        var ground = TileRenderer.tile, body = TileRenderer.ink, label = TileRenderer.ink, meta = TileRenderer.dim
        var chip = TileRenderer.chip, ring = TileRenderer.accent, track = TileRenderer.track
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

    /// One tile in `r` (the menu's are 120 tall; a bigger one is drawn at this size under a scaled context), at
    /// `ms` of the animation clock. Needs a flipped view.
    public static func draw(_ t: AgentTile, in r: NSRect, hovered: Bool = false, ms: UInt64, _ ctx: CGContext) {
        let l = Self.look(t)
        l.ground.setFill()
        r.fill()
        ctx.saveGState()
        ctx.clip(to: r)
        if t.hungry && t.kind != "wait" { drawStripes(r, ms: ms, ctx) } else if t.kind == "run" { drawRunGround(r, clear: textBoxes(t, width: r.width), ms: ms) }
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

        drawText(t.name, size: Self.nameSize, weight: .medium, color: l.label, at: NSPoint(x: r.minX + 8, y: r.minY + Self.nameY),
                 maxW: r.width - 16)
        drawText(t.line, size: Self.lineSize, color: l.meta, at: NSPoint(x: r.minX + 8, y: r.minY + Self.lineY), maxW: r.width - 16)
    }

    private static func drawCritter(_ t: AgentTile, at o: NSPoint, look l: Look, ms: UInt64) {
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
    private static func drawRing(_ r: NSRect, fraction: CGFloat, _ l: Look, _ ctx: CGContext) {
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

    private static let nameSize: CGFloat = 12, lineSize: CGFloat = 11
    private static let nameY: CGFloat = 76, lineY: CGFloat = 94

    /// The boxes behind the name and the status line, in tile coordinates, as wide as the text: the ground stops
    /// there and nowhere else, as the cube's text boxes do.
    private static func textBoxes(_ t: AgentTile, width w: CGFloat) -> [NSRect] {
        func box(_ s: String, _ font: NSFont, y: CGFloat, h: CGFloat) -> NSRect {
            let tw = min((s as NSString).size(withAttributes: [.font: font]).width, w - 16)
            return NSRect(x: (w - tw) / 2 - 4, y: y - 1, width: tw + 8, height: h)
        }
        return [box(t.name, .monospacedSystemFont(ofSize: Self.nameSize, weight: .medium), y: Self.nameY, h: 18),
                box(t.line, .monospacedSystemFont(ofSize: Self.lineSize, weight: .regular), y: Self.lineY, h: 17)]
    }

    /// Running: agents-deck's dot matrix, faint accent dots that a diagonal wave brightens, and four rising specks.
    private static func drawRunGround(_ r: NSRect, clear: [NSRect], ms: UInt64) {
        func covered(_ x: CGFloat, _ y: CGFloat, _ size: CGFloat) -> Bool {
            clear.contains { $0.intersects(NSRect(x: x, y: y, width: size, height: size)) }
        }
        var y = TileGround.dotInset
        while y < r.height - 2 {
            var x = TileGround.dotInset
            while x < r.width - 2 {
                if !covered(x, y, 1.5) {
                    Self.accent.withAlphaComponent(TileGround.dotAlpha(dx: x, dy: y, w: r.width, h: r.height, ms: ms)).setFill()
                    NSRect(x: r.minX + x, y: r.minY + y, width: 1.5, height: 1.5).fill(using: .sourceOver)
                }
                x += TileGround.dotPitch
            }
            y += TileGround.dotPitch
        }
        for i in 0..<4 {
            let p = TileGround.particle(i, w: r.width, h: r.height, ms: ms)
            guard p.alpha > 0, !covered(p.x, p.y, TileGround.particleSize) else { continue }
            Self.accent.withAlphaComponent(p.alpha).setFill()
            NSRect(x: r.minX + p.x, y: r.minY + p.y, width: TileGround.particleSize, height: TileGround.particleSize)
                .fill(using: .sourceOver)
        }
    }

    /// Hungry: the board's 135° accent stripes, 12 px on 24, sliding.
    private static func drawStripes(_ r: NSRect, ms: UInt64, _ ctx: CGContext) {
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

    private static func drawText(_ s: String, size: CGFloat, weight: NSFont.Weight = .regular, color: NSColor, at p: NSPoint,
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
