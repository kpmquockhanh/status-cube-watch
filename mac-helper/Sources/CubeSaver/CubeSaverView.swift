import AppKit
import CubeLinkCore
import CubeLinkUI
import OSLog
import ScreenSaver

/// The "Claude Cube" screen saver: every agent of the bridge's board (`GET /api/board`, `Board`) as the menu's
/// tiles (`TileRenderer`), scaled up to fill the screen, animated at 20 frames/s. Over the lock screen it is what
/// stays on view until a key or the mouse wakes the Mac. It reads the bridge on 127.0.0.1 at the port baked into
/// its Info.plist (`CubePort`, from `install.sh`), every 2 s; the bridge's answer is all it shows.
@objc(CubeSaverView)
final class CubeSaverView: ScreenSaverView {
    private static let tileW: CGFloat = 144, gap: CGFloat = 8, captionH: CGFloat = 28
    private static let fetchEvery: TimeInterval = 2, staleAfter: TimeInterval = 15
    private static let maxScale: CGFloat = 3.5
    // The lock screen draws over a running saver: the date and the large clock at the top, down to about 28 % of
    // the height, and the avatar, name and password field at the bottom, from about 78 %. The tiles keep clear.
    private static let clearTop: CGFloat = 0.30, clearBottom: CGFloat = 0.24

    private let port: Int
    private let session: URLSession
    private var board: Board?
    private var lastOk: TimeInterval = 0
    private var lastFetch: TimeInterval = 0
    private var inFlight = false
    private var failed = false  // the last answer was not the board
    private var stopped = false
    private var reachable: Bool?  // the last answer's outcome, logged when it changes
    private static let log = Logger(subsystem: "com.claude-cube.saver", category: "board")

    override init?(frame: NSRect, isPreview: Bool) {
        port = Bundle(for: CubeSaverView.self).object(forInfoDictionaryKey: "CubePort") as? Int ?? 8787
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 2
        session = URLSession(configuration: config)
        super.init(frame: frame, isPreview: isPreview)
        animationTimeInterval = 1.0 / 20
        // legacyScreenSaver keeps a stopped saver's view alive (and may never call stopAnimation): stop reading
        // the bridge when the system says the saver has ended.
        DistributedNotificationCenter.default().addObserver(self, selector: #selector(willStop),
                                                            name: .init("com.apple.screensaver.willstop"), object: nil)
    }

    required init?(coder: NSCoder) { fatalError() }

    deinit { DistributedNotificationCenter.default().removeObserver(self) }

    override var isFlipped: Bool { true }
    override var hasConfigureSheet: Bool { false }

    override func startAnimation() {
        // A fresh run draws nothing until its first answer, not the last run's tiles or their age.
        stopped = false
        board = nil
        failed = false
        super.startAnimation()
        fetch()
    }

    override func stopAnimation() {
        stopped = true
        super.stopAnimation()
    }

    @objc private func willStop() { stopped = true }

    override func animateOneFrame() {
        guard !stopped else { return }
        if ProcessInfo.processInfo.systemUptime - lastFetch >= Self.fetchEvery { fetch() }
        needsDisplay = true
    }

    private func fetch() {
        guard !inFlight else { return }
        inFlight = true
        lastFetch = ProcessInfo.processInfo.systemUptime
        session.dataTask(with: URLRequest(url: Board.url(port: port))) { [weak self] data, response, _ in
            let ok = (response as? HTTPURLResponse)?.statusCode == 200
            let board = ok ? data.flatMap(Board.parse) : nil
            DispatchQueue.main.async {
                guard let self else { return }
                self.inFlight = false
                self.failed = board == nil
                if let board {
                    self.board = Board(caption: TileRenderer.covered(board.caption), tiles: board.tiles.map(TileRenderer.covered))
                    self.lastOk = ProcessInfo.processInfo.systemUptime
                }
                if self.reachable != (board != nil) {
                    self.reachable = board != nil
                    let port = self.port
                    if board != nil {
                        Self.log.notice("bridge reachable on port \(port, privacy: .public)")
                    } else {
                        Self.log.error("bridge not reachable on port \(port, privacy: .public)")
                    }
                }
            }
        }.resume()
    }

    // MARK: - drawing

    override func draw(_ dirty: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        TileRenderer.bg.setFill()
        bounds.fill()
        let ms = TileRenderer.nowMs
        // Until the first answer nothing is drawn (legacyScreenSaver draws the view before startAnimation, and a
        // restarted one for seconds before it); a failed answer, or none for staleAfter, reads as a bridge that is down.
        guard let board, ProcessInfo.processInfo.systemUptime - lastOk < Self.staleAfter else {
            if failed || board != nil { message("cube bridge not running") }
            return
        }
        guard !board.tiles.isEmpty else {
            message("no agents")
            return
        }
        let n = board.tiles.count
        let area = safeArea
        let (cols, scale) = Self.grid(n, in: area.size)
        let rows = (n + cols - 1) / cols
        let w = CGFloat(cols) * Self.tileW + CGFloat(cols - 1) * Self.gap
        let h = Self.captionH + CGFloat(rows) * TileRenderer.tileH + CGFloat(rows - 1) * Self.gap
        // A slow drift of a few points, so nothing sits on one spot of the panel for hours.
        let t = Double(ms) / 1000
        let drift = NSPoint(x: 10 * sin(t / 47), y: 8 * sin(t / 61))
        ctx.saveGState()
        ctx.translateBy(x: (area.midX - w * scale / 2 + drift.x).rounded(), y: (area.midY - h * scale / 2 + drift.y).rounded())
        ctx.scaleBy(x: scale, y: scale)
        let waiting = board.tiles.contains { $0.kind == "wait" }
        text(board.caption.lowercased(), size: 13, color: waiting ? TileRenderer.accent : TileRenderer.dim,
             in: NSRect(x: 0, y: 0, width: w, height: Self.captionH), centered: false)
        let lastRow = n - (rows - 1) * cols  // tiles in the last row, centred under the others
        for (i, tile) in board.tiles.enumerated() {
            let col = CGFloat(i % cols), row = CGFloat(i / cols)
            let indent = i / cols == rows - 1 ? CGFloat(cols - lastRow) * (Self.tileW + Self.gap) / 2 : 0
            let r = NSRect(x: indent + col * (Self.tileW + Self.gap), y: Self.captionH + row * (TileRenderer.tileH + Self.gap),
                           width: Self.tileW, height: TileRenderer.tileH)
            TileRenderer.draw(tile, in: r, ms: ms, ctx)
        }
        ctx.restoreGState()
    }

    /// Where the tiles go: clear of the lock screen's clock and login, inside a margin for the drift. The preview
    /// in System Settings has neither and uses the whole view.
    private var safeArea: NSRect {
        let b = bounds.insetBy(dx: bounds.width * 0.06, dy: 0)
        guard !isPreview else { return b.insetBy(dx: 0, dy: bounds.height * 0.08) }
        return NSRect(x: b.minX, y: bounds.height * Self.clearTop, width: b.width,
                      height: bounds.height * (1 - Self.clearTop - Self.clearBottom))
    }

    /// The column count that draws n tiles (plus the caption) largest in `area`, and that scale.
    static func grid(_ n: Int, in area: NSSize) -> (cols: Int, scale: CGFloat) {
        var best = (cols: 1, scale: CGFloat(0))
        for cols in 1...max(1, n) {
            let rows = (n + cols - 1) / cols
            let w = CGFloat(cols) * tileW + CGFloat(cols - 1) * gap
            let h = captionH + CGFloat(rows) * TileRenderer.tileH + CGFloat(rows - 1) * gap
            let s = min(area.width / w, area.height / h, maxScale)
            if s > best.scale + 0.001 { best = (cols, s) }
        }
        return best
    }

    private func message(_ s: String) {
        let size = max(12, bounds.height / 40)
        text(s, size: size, color: TileRenderer.dim,
             in: NSRect(x: 0, y: safeArea.midY - size * 0.7, width: bounds.width, height: size * 1.6))
    }

    private func text(_ s: String, size: CGFloat, color: NSColor, in r: NSRect, centered: Bool = true) {
        let style = NSMutableParagraphStyle()
        style.alignment = centered ? .center : .left
        style.lineBreakMode = .byTruncatingTail
        (s as NSString).draw(with: r, options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine],
                             attributes: [.font: NSFont.monospacedSystemFont(ofSize: size, weight: .medium),
                                          .foregroundColor: color, .paragraphStyle: style])
    }
}
