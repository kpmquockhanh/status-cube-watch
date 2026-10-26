import AppKit
import CubeLinkCore
import CubeLinkUI

/// The Agents card as the web board draws it (`bridge/board/board.css`, `.tile`): two columns of square-cornered
/// tiles on the deck's black, each with its critter (`Critter`), name, status line, Claude chip and the context
/// gauge round its edge. While the menu is open the critters, the running dot wave and the hungry stripes move as
/// the cube and the board do (`Critter`, `TileGround`), on the system's uptime so a menu rebuilt by a new payload
/// carries on where it was. A click on a tile calls `onTile`.
final class AgentTilesView: NSView {
    private static let pad: CGFloat = 6, gap: CGFloat = 6, margin: CGFloat = 8
    static let tileH = TileRenderer.tileH

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

    private var animates: Bool { TileRenderer.animates(tiles) }

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

    override func draw(_ dirty: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        TileRenderer.bg.setFill()
        NSRect(x: Self.margin, y: 0, width: bounds.width - Self.margin * 2, height: bounds.height).fill()
        let ms = TileRenderer.nowMs
        for (i, t) in tiles.enumerated() { TileRenderer.draw(t, in: tileRect(i), hovered: i == hovered, ms: ms, ctx) }
    }
}
