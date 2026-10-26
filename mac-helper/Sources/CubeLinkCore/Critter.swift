import Foundation

/// The critter of an Agents tile as rectangles in sprite cells at a moment, for the menu's tile grid: a port of the
/// firmware's `critter.cpp` and `hat_art.cpp` (themselves agents-deck's sprite; the web board's is
/// `bridge/board/critter.js`), so the menu moves as the cube and the board do. Edit them together. Pure: the menu
/// view only paints what this returns.
public enum Critter {
    /// What a shape is filled with. `body` and `cut` come from the tile's state (the board's --body / --cut).
    public enum Paint: Equatable {
        case body, cut, flag, accent
        case rgb(UInt32)
    }

    public struct Rect: Equatable {
        public let x, y, w, h: Double
        public let paint: Paint
        public let alpha: Double  // 0...1 over what is below; never 0 (such a shape is left out)

        public init(x: Double, y: Double, w: Double, h: Double, paint: Paint, alpha: Double = 1) {
            self.x = x; self.y = y; self.w = w; self.h = h; self.paint = paint; self.alpha = alpha
        }
    }

    /// The region every pose and hat fits in at any moment, in cells (critter.h: x -5...20, y -8...10).
    public static let bounds = (x: -5.0, y: -8.0, w: 25.0, h: 18.0)

    public static let hatShapes = ["hard hat", "cap", "beanie", "helmet", "wizard", "beret", "chef", "crown",
                                   "headphones", "propeller", "top hat", "cowboy", "bandana"]
    /// Main colour and shade, in agents-deck's order: teal, blue, yellow, green, purple, pink, sky, ink.
    public static let hatColors: [(UInt32, UInt32)] = [
        (0x4FB3A6, 0x2F7F75), (0x6F8FD8, 0x4A63A3), (0xE8B94A, 0xB08429), (0x6DBE6A, 0x468A44),
        (0xA68BD8, 0x7660A6), (0xE58BB0, 0xB05E80), (0x5EC4D6, 0x3A8E9C), (0xE8E4DC, 0xA8A39A),
    ]

    /// `h` = shape * 8 + colour; anything outside 0...103 is no hat.
    public static func hat(code: Int?) -> (shape: Int, color: Int)? {
        guard let code, code >= 0, code < hatShapes.count * 8 else { return nil }
        return (code / 8, code % 8)
    }

    // MARK: - drawing state (critter.cpp's Pen and Out)

    /// The body group's transform, in cells: the bob, the squash and the mini critter move or scale everything
    /// drawn through it.
    private struct Pen {
        var sx = 1.0, sy = 1.0, tx = 0.0, ty = 0.0
        var opa = 1.0

        func moved(_ dx: Double, _ dy: Double) -> Pen {
            var p = self
            p.tx += sx * dx
            p.ty += sy * dy
            return p
        }

        func with(opa o: Double) -> Pen {
            var p = self
            p.opa = o
            return p
        }
    }

    private struct Out {
        var rects: [Rect] = []
        let ms: UInt64
        let hatColor: (UInt32, UInt32)

        mutating func put(_ p: Pen, _ x: Double, _ y: Double, _ w: Double, _ h: Double, _ paint: Paint) {
            guard p.opa > 0 else { return }
            rects.append(Rect(x: p.tx + p.sx * x, y: p.ty + p.sy * y, w: p.sx * w, h: p.sy * h, paint: paint, alpha: p.opa))
        }

        /// Where we are in a cycle of `period` ms, 0..<1.
        func phase(_ period: UInt64, delay: UInt64 = 0) -> Double { Double((ms + delay) % period) / Double(period) }
        func laOn(_ period: UInt64) -> Bool { phase(period) < 0.5 }
    }

    private static let legsA: [Double] = [2, 4, 9, 11]
    private static let legsB: [Double] = [3, 5, 8, 10]

    private static func legs(_ o: inout Out, _ p: Pen, _ xs: [Double]) {
        for x in xs { o.put(p, x, 6, 1, 1.6, .body) }
    }

    private static func arms(_ o: inout Out, _ p: Pen, _ l: Double, _ r: Double) {
        o.put(p, 0, l, 1, 2, .body)
        o.put(p, 13, r, 1, 2, .body)
    }

    private static func eyesOpenAt(_ o: inout Out, _ p: Pen, _ y: Double, _ h: Double) {
        o.put(p, 4, y, 1, h, .cut)
        o.put(p, 9, y, 1, h, .cut)
    }

    /// A straight stroke from (x1, y1) to (x2, y2) as `n` small squares: a stroked eye.
    private static func line(_ o: inout Out, _ p: Pen, _ x1: Double, _ y1: Double, _ x2: Double, _ y2: Double, _ n: Int) {
        for i in 0..<n {
            let t = n > 1 ? Double(i) / Double(n - 1) : 0
            o.put(p, x1 + (x2 - x1) * t - 0.3, y1 + (y2 - y1) * t - 0.3, 0.6, 0.6, .cut)
        }
    }

    /// Bitmap rows ('#' cells) of cell size w x h from (x0, y0).
    private static func pixels(_ o: inout Out, _ p: Pen, _ rows: [String], _ x0: Double, _ w: Double, _ h: Double,
                               _ y0: Double, _ paint: Paint = .body) {
        for (r, row) in rows.enumerated() {
            for (c, ch) in row.enumerated() where ch == "#" { o.put(p, x0 + Double(c) * w, y0 + Double(r) * h, w, h, paint) }
        }
    }

    private static func dots(_ o: inout Out, _ p0: Pen) {
        let steps = [0.2, 0.6, 1.0]  // .2 -> 1, in three steps
        let p = p0.with(opa: p0.opa * steps[min(2, Int(o.phase(1200) * 3))])
        o.put(p, 14.6, -2.6, 0.9, 0.9, .body)
        o.put(p, 15.9, -2.6, 0.9, 0.9, .body)
        o.put(p, 17.2, -2.6, 0.8, 0.9, .body)
    }

    /// The little critter a delegating agent shows.
    private static func mini(_ o: inout Out, _ p: Pen, _ ox: Double, _ oy: Double, _ s: Double) {
        var q = p
        q.tx += q.sx * ox
        q.ty += q.sy * oy
        q.sx *= s
        q.sy *= s
        let b = o.laOn(500) ? q : q.moved(0, -0.5)
        o.put(b, 1, 0, 12, 6, .body)
        o.put(b, 0, 2, 1, 2, .body)
        o.put(b, 13, 2, 1, 2, .body)
        o.put(b, 4, 1.5, 1, 2, .cut)
        o.put(b, 9, 1.5, 1, 2, .cut)
        legs(&o, q, o.laOn(500) ? legsA : legsB)
    }

    /// A "z" of size s with its top-left at (x, y): bar, diagonal, bar.
    private static func zed(_ o: inout Out, _ p: Pen, _ x: Double, _ y: Double, _ s: Double) {
        let t = s / 4
        o.put(p, x, y, s, t, .body)
        o.put(p, x + s - t, y + t, t, t, .body)
        o.put(p, x + s / 2 - t / 2, y + s / 2 - t / 2, t, t, .body)
        o.put(p, x, y + s - 2 * t, t, t, .body)
        o.put(p, x, y + s - t, s, t, .body)
    }

    // MARK: - hats

    private struct HatFrame {
        let x, top: Double
        let rows: [String]  // a = hat colour, b = shade, c = highlight, d = near-black, '.' = empty
        var extras: [(Double, Double, Character)] = []  // absolute cells
    }

    private struct HatArt {
        let ms: UInt64  // per frame while working
        let still: HatFrame
        let frames: [HatFrame]
    }

    private static func f(_ x: Double, _ top: Double, _ rows: [String], _ extras: [(Double, Double, Character)] = []) -> HatFrame {
        HatFrame(x: x, top: top, rows: rows, extras: extras)
    }

    // hat_art.cpp, in agents-deck's hash order.
    private static let hatArt: [HatArt] = {
        let hard = ["....aaaaa.....", "...acaaaaa....", "..aaaaaaaaa...", ".bbbbbbbbbbbb."]
        let cap = ["........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"]
        let beanie = [".....cc.......", "....aaaaaa....", "...aaaaaaaa...", "..babababab..."]
        let wizard = ["........a.....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."]
        let beret = ["......b.......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."]
        let chef = ["...cc.cc.cc...", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."]
        let crownTail = ["...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."]
        let phones = ["....aaaaaa....", "...a......a...", "..a........a.."]
        let propTail = [".......d......", "....ababab....", "...aaaaaaaa..."]
        let topHat = ["...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."]
        let cowboy = [".....aaaa.....", "....aabbaa....", "b...aaaaaa...b", "bbbbbbbbbbbbbb"]
        let bandana = ["..aaaaaaaaaa..", "..acacacacac.."]
        let knot: [(Double, Double, Character)] = [(1, -1, "b"), (0, -2, "b"), (0, 0, "b"), (-1, 0, "b")]
        func helmet(_ row: String) -> [String] { ["....aaaaaa....", "...aaaaaaaa...", row, "..bbbbbbbbbb.."] }
        func beretTop(_ row: String) -> [String] { [row] + Array(beret.dropFirst()) }
        return [
            HatArt(ms: 400, still: f(0, -4, hard), frames: [
                f(0, -4, ["....aaaaa.....", "...acaaaaa....", "..aaaaccaaa...", ".bbbbbbbbbbbb."]),
                f(0, -4, ["....aaaaa.....", "...acaaaaa....", "..aaaabbaaa...", ".bbbbbbbbbbbb."]),
            ]),
            HatArt(ms: 300, still: f(-3, -3, cap), frames: [
                f(-3, -3, cap), f(-3, -3, cap),
                f(-3, -3, ["........aaaaaa", ".......aaaaacaa", "...bbbbbbbbbbbb"], [(-3, 0, "b"), (-2, 0, "b")]),
                f(-3, -3, cap),
            ]),
            HatArt(ms: 350, still: f(0, -4, beanie), frames: [
                f(0, -4, beanie), f(0, -3, Array(beanie.dropFirst()), [(5, -5, "c"), (6, -5, "c")]),
            ]),
            HatArt(ms: 280, still: f(0, -4, helmet("..aaaacaaaaa..")), frames: [
                f(0, -4, helmet("..acaaaaaaaa..")), f(0, -4, helmet("..aaaaacaaaa..")),
                f(0, -4, helmet("..aaaaaaaaca..")), f(0, -4, helmet("..aaaaaaaaaa..")),
            ]),
            HatArt(ms: 300, still: f(0, -5, wizard), frames: [
                f(0, -5, wizard), f(0, -5, [".........a...."] + Array(wizard.dropFirst()), [(11, -6, "c")]),
                f(0, -5, wizard), f(0, -5, [".......a......"] + Array(wizard.dropFirst())),
            ]),
            HatArt(ms: 300, still: f(0, -4, beret), frames: [
                f(0, -4, beret), f(0, -4, beretTop(".......b......")), f(0, -4, beret), f(0, -4, beretTop(".....b........")),
            ]),
            HatArt(ms: 450, still: f(0, -4, chef), frames: [
                f(0, -4, chef),
                f(0, -5, ["....cccccc....", "..cccccccccc..", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."]),
            ]),
            HatArt(ms: 280, still: f(0, -4, ["...a..aa..a..."] + crownTail), frames: [
                f(0, -4, ["...c..aa..a..."] + crownTail), f(0, -4, ["...a..ca..a..."] + crownTail),
                f(0, -4, ["...a..ac..a..."] + crownTail), f(0, -4, ["...a..aa..a..."] + crownTail),
            ]),
            HatArt(ms: 320, still: f(0, -3, phones), frames: [
                f(0, -3, phones, [(-2, -1, "c"), (-1, -2, "c")]), f(0, -3, phones, [(-2, -2, "c"), (-1, -3, "c")]),
                f(0, -3, phones, [(-3, -3, "c"), (-2, -4, "c"), (-1, -1, "c")]), f(0, -3, phones, [(-2, -2, "c")]),
            ]),
            HatArt(ms: 120, still: f(0, -4, ["..cccc.aaaa..."] + propTail), frames: [
                f(0, -4, ["..cccc.aaaa..."] + propTail), f(0, -4, ["....cc.aa....."] + propTail),
                f(0, -4, ["..aaaa.cccc..."] + propTail), f(0, -4, ["....aa.cc....."] + propTail),
            ]),
            HatArt(ms: 450, still: f(0, -5, topHat), frames: [
                f(0, -5, topHat), f(0, -5, topHat), f(0, -6, topHat), f(0, -5, topHat),
            ]),
            HatArt(ms: 350, still: f(0, -4, cowboy), frames: [
                f(0, -4, cowboy),
                f(0, -4, [".....aaaa.....", "b...aabbaa....", "b...aaaaaa....", ".bbbbbbbbbbbbb"]),
                f(0, -4, cowboy),
                f(0, -4, [".....aaaa.....", "....aabbaa...b", "....aaaaaa...b", "bbbbbbbbbbbbb."]),
            ]),
            HatArt(ms: 200, still: f(0, -1, bandana, knot), frames: [
                f(0, -1, bandana, knot),
                f(0, -1, bandana, [(1, -1, "b"), (0, -1, "b"), (-1, -2, "b"), (0, 0, "b"), (-1, 1, "b")]),
                f(0, -1, bandana, [(1, -1, "b"), (0, -2, "b"), (-1, -2, "b"), (0, 0, "b"), (-1, 0, "b")]),
            ]),
        ]
    }()

    private static func hatPaint(_ k: Character, _ color: (UInt32, UInt32)) -> Paint? {
        switch k {
        case "a": .rgb(color.0)
        case "b": .rgb(color.1)
        case "c": .rgb(0xF7F4EC)
        case "d": .rgb(0x0A0A0A)
        default: nil
        }
    }

    private static func hatFrame(_ o: inout Out, _ p: Pen, _ fr: HatFrame) {
        for (r, row) in fr.rows.enumerated() {
            let cells = Array(row)
            var c = 0
            while c < cells.count {  // a run of one colour is one rectangle
                guard let paint = hatPaint(cells[c], o.hatColor) else { c += 1; continue }
                var e = c + 1
                while e < cells.count && cells[e] == cells[c] { e += 1 }
                o.put(p, fr.x + Double(c), fr.top + Double(r), Double(e - c), 1, paint)
                c = e
            }
        }
        for (x, y, k) in fr.extras { o.put(p, x, y, 1, 1, hatPaint(k, o.hatColor) ?? .rgb(0x0A0A0A)) }
    }

    private static func hat(_ o: inout Out, _ p: Pen, shape: Int, faded: Bool, animate: Bool) {
        let art = hatArt[shape]
        let q = faded ? p.with(opa: p.opa * 0.45) : p
        hatFrame(&o, q, animate ? art.frames[Int((o.ms / art.ms) % UInt64(art.frames.count))] : art.still)
        if hatShapes[shape] == "headphones" {
            o.put(q, 0, 0, 1, 2, .rgb(o.hatColor.1))
            o.put(q, 13, 0, 1, 2, .rgb(o.hatColor.1))
        }
    }

    private static let workingPoses: Set<String> = ["run", "think", "read", "write", "bash", "web", "plan", "delegate", "compact"]
    private static let poses: Set<String> = workingPoses.union(["idle", "start", "wait", "err", "exit", "hungry", "celebrate"])

    // MARK: - the sprite

    /// The critter for a pose (`p`; unknown means asleep) wearing hat `h`, at `ms` on the animation clock, back to
    /// front. `ms` 0 is each animation's first frame.
    public static func shapes(pose name: String, hat code: Int?, ms: UInt64 = 0) -> [Rect] {
        let hatId = hat(code: code)
        var o = Out(ms: ms, hatColor: hatColors[hatId?.color ?? 7])
        let root = Pen()
        let animateHat = workingPoses.contains(name)
        let pose = poses.contains(name) ? (name == "write" ? "bash" : name) : "idle"  // writing looks like bash

        var g = root  // the body group: bob and squash move it
        var step: UInt64 = 0  // legs stepping period (0: still)
        var al = 2.0, ar = 2.0  // arm rows
        var armsStep = false, noArms = false, noFeet = false
        switch pose {
        case "think":
            // Pondering, not asleep: a slow bob on the glance's 1.2 s, and the raised hand taps the chin.
            ar = o.laOn(800) ? 1 : 0
            if !o.laOn(1200) { g = g.moved(0, -0.5) }
        case "bash":
            step = 200
            if !o.laOn(200) { g = g.moved(0, -0.5) }
        case "delegate", "run":
            step = 500
            armsStep = true
            if !o.laOn(500) { g = g.moved(0, -0.5) }
        case "celebrate":
            al = 0; ar = 0
            if !o.laOn(400) { g = g.moved(0, -1.5) }
        case "err":
            al = 0; ar = 0
        case "compact":
            let q = Int(o.phase(1200) * 4)
            let sx = q == 0 ? 1 : q == 2 ? 1.2 : 1.1, sy = q == 0 ? 1 : q == 2 ? 0.62 : 0.8
            g.tx += g.sx * 7 * (1 - sx)  // scale around cell (7, 7)
            g.ty += g.sy * 7 * (1 - sy)
            g.sx *= sx
            g.sy *= sy
        case "exit":
            noArms = true; noFeet = true
        default: break
        }

        // The body group.
        o.put(g, 1, 0, 12, 6, .body)
        if pose == "exit" { o.put(g, 2, 1, 10, 4, .cut) }
        if !noArms {
            if armsStep {
                if o.laOn(500) { arms(&o, g, 2, 1) } else { arms(&o, g, 1, 2) }
            } else {
                arms(&o, g, al, ar)
            }
        }
        let fp = g.moved(0, -0.5)  // the block's face sits half a row higher
        switch pose {
        case "bash", "plan", "delegate", "run", "start": eyesOpenAt(&o, fp, 2, 2)
        case "wait": eyesOpenAt(&o, fp, 1, 2)
        case "think": eyesOpenAt(&o, fp.moved(o.laOn(1200) ? -1 : 1, 0), 1, 1.5)
        case "read": eyesOpenAt(&o, fp.moved([0, -1, 0, 1][min(3, Int(o.phase(1200) * 4))], 0), 2, 2)
        case "web":
            o.put(fp, 4.4, 1, 1, 2, .cut)
            o.put(fp, 9.4, 1, 1, 2, .cut)
        case "hungry":
            o.put(fp, 4.6, 1, 1, 2, .cut)
            o.put(fp, 9.6, 1, 1, 2, .cut)
            let open = o.laOn(800)
            o.put(fp, 6.2, open ? 4.2 : 4.8, 2.2, open ? 1.6 : 0.5, .cut)
        case "celebrate":  // "^ ^"
            for ex in [4.5, 9.5] {
                line(&o, fp, ex - 1, 3.3, ex, 2.2, 3)
                line(&o, fp, ex, 2.2, ex + 1, 3.3, 3)
            }
        case "err":  // an "x" per eye
            for ex in [3.8, 8.8] {
                line(&o, fp, ex - 0.4, 1.8, ex + 1.4, 4, 4)
                line(&o, fp, ex + 1.4, 1.8, ex - 0.4, 4, 4)
            }
        case "compact":  // "> <"
            line(&o, fp, 3.6, 1.9, 4.8, 2.7, 3)
            line(&o, fp, 4.8, 2.7, 3.6, 3.5, 3)
            line(&o, fp, 10.4, 1.9, 9.2, 2.7, 3)
            line(&o, fp, 9.2, 2.7, 10.4, 3.5, 3)
        case "exit":
            o.put(fp, 4, 3, 1, 1, .body)
            o.put(fp, 9, 3, 1, 1, .body)
        default:  // asleep
            o.put(fp, 3.5, 3.2, 2, 0.55, .cut)
            o.put(fp, 8.5, 3.2, 2, 0.55, .cut)
        }

        if let hatId { hat(&o, g, shape: hatId.shape, faded: pose == "exit", animate: animateHat) }
        if pose == "celebrate" {  // the finish flag in the raised hand, waving
            o.put(g, 14, -4, 1, 6, .flag)
            let ph = o.laOn(500) ? 0 : 1
            for cx in 0..<3 {
                for cy in 0..<2 {
                    o.put(g, 15 + Double(cx), -4 + Double(cy), 1, 1, (cx + cy + ph) % 2 == 1 ? .cut : .flag)
                }
            }
        }

        // Feet, outside the body group (the bob does not move them; the hop does).
        if !noFeet { legs(&o, pose == "celebrate" ? g : root, step == 0 || o.laOn(step) ? legsA : legsB) }

        // The prop beside it says what it is doing.
        let b = root
        switch pose {
        case "think":  // the thought rises dot by dot, holds, then pops
            let pp = o.phase(1200)
            if pp < 0.85 {
                o.put(b, 14.3, -0.9, 0.6, 0.6, .body)
                if pp >= 0.25 { o.put(b, 15.3, -2.1, 0.9, 0.9, .body) }
                if pp >= 0.5 { o.put(b, 16.5, -3.8, 1.3, 1.3, .body) }
            }
        case "read": pixels(&o, b, [".##..", "#..#.", "#..#.", ".##..", "....#"], 14.6, 0.75, 0.75, -4)
        case "bash":
            pixels(&o, b, ["#..", ".#.", "#.."], 14.5, 0.7, 0.8, -3.7)
            if o.laOn(1000) { o.put(b, 16.5, -1.7, 1.2, 0.5, .body) }
        case "web":
            pixels(&o, b, o.laOn(800) ? [".###.", "#.#.#", "#####", "#.#.#", ".###."] : [".###.", "##.##", "#####", "##.##", ".###."],
                   14.4, 0.72, 0.72, -4.2)
        case "plan":
            let pp = o.phase(2000)
            for i in 0..<3 {
                let y = -3.9 + Double(i) * 1.25
                o.put(b, 14.4, y, 0.9, 0.9, .body)
                if pp < Double(i + 1) * 0.25 { o.put(b, 14.6, y + 0.2, 0.5, 0.5, .cut) }
                o.put(b, 15.6, y + 0.25, 2.2, 0.45, .body)
            }
        case "delegate": mini(&o, b, 14.4, 3.2, 0.26)
        case "hungry":
            pixels(&o, o.laOn(600) ? b : b.moved(0, 0.6), [".###.", "##.##", "#####", "#.###", ".###."], 14.3, 0.75, 0.75, -4.2)
        case "celebrate":  // two twinkles
            for i in 0..<2 {
                let s = o.phase(600, delay: i == 1 ? 400 : 0) >= 0.34 ? b.with(opa: 0.45) : b
                let sx = i == 1 ? 18.2 : 16.4, sy = i == 1 ? -4.4 : -6.2
                o.put(s, sx - 0.5, sy, 1.5, 0.5, .body)
                o.put(s, sx, sy - 0.5, 0.5, 1.5, .body)
            }
        case "run", "start": dots(&o, b)
        case "wait":  // a blinking "?"
            pixels(&o, o.phase(1000) >= 0.6 ? b.with(opa: 0.15) : b, ["###", "..#", ".#.", "...", ".#."], 14.5, 1.1, 0.9, -4)
        case "err": pixels(&o, b, ["#", "#", "#", ".", "#"], 15.6, 1.2, 0.9, -4, .accent)
        case "compact":
            let q = Int(o.phase(1200) * 4)
            let p1 = q == 0 ? 0 : q == 2 ? 1.1 : 0.4, p2 = q == 0 ? 0 : q == 2 ? 2.2 : 0.8
            o.put(b, 14.6, -3.8 + p2, 3.2, 0.6, .body)
            o.put(b, 14.6, -2.7 + p1, 3.2, 0.6, .body)
            o.put(b, 14.6, -1.6, 3.2, 0.6, .body)
        case "exit": break
        default:  // asleep: two z's drifting up
            let k = min(3, Int(o.phase(2400) * 4))
            let z = b.moved(0, [0, -0.3, -0.6, -0.3][k]).with(opa: [0.9, 0.65, 0.4, 0.65][k])
            zed(&o, z, 14.8, -1.8, 1.6)
            zed(&o, z, 16.6, -3.4, 1.2)
        }
        return o.rects
    }

    /// Whether a pose changes over time (every one does but the exited agent).
    public static func animates(pose: String) -> Bool { pose != "exit" }
}

/// A running tile's ground and a hungry tile's stripes over time: the numbers of the firmware's
/// `agents_layout.h` (`runDotAlpha`, `runParticle`) and the board's `.tile.hungry`.
public enum TileGround {
    public static let dotPitch = 6.0, dotInset = 4.0
    static let waveMs: UInt64 = 2400, sweepMs: UInt64 = 1800
    static let base = 38.0 / 255, peak = 160.0 / 255, band = 0.25

    /// The accent's alpha for the dot at (dx, dy) inside a w x h tile at `ms`: ~15 %, raised towards ~63 % near
    /// a soft diagonal crest that runs bottom-left to top-right in 1.8 s, then rests 0.6 s.
    public static func dotAlpha(dx: Double, dy: Double, w: Double, h: Double, ms: UInt64) -> Double {
        let t = ms % waveMs
        guard t < sweepMs, w + h > 2 else { return base }
        let f = -band + (1 + 2 * band) * Double(t) / Double(sweepMs)
        let d = (dx + (h - 1 - dy)) / (w + h - 2)  // 0 bottom-left .. 1 top-right
        var k = 1 - abs(d - f) / band
        guard k > 0 else { return base }
        k = k * k * (3 - 2 * k)  // smoothstep: a soft crest
        return base + k * (peak - base)
    }

    public static let particleSize = 2.0

    /// Speck `i` (0...3) of a w x h tile at `ms`: its top-left inside the tile and its alpha (0: not drawn). Each
    /// has its own column, period and start, rising from the bottom, fading in, holding, fading out.
    public static func particle(_ i: Int, w: Double, h: Double, ms: UInt64) -> (x: Double, y: Double, alpha: Double) {
        let xPct = [22.0, 71, 46, 88], period: [UInt64] = [3000, 3600, 2700, 3300], offset: [UInt64] = [0, 1400, 700, 2200]
        guard (0..<4).contains(i) else { return (0, 0, 0) }
        let t = Double((ms + offset[i]) % period[i]) / Double(period[i])
        let bottom = h - 6 - particleSize, top = 6.0
        let a = t < 0.15 ? t / 0.15 : t > 0.6 ? (1 - t) / 0.4 : 1
        return ((w * xPct[i] / 100 - particleSize / 2).rounded(.down), (bottom - t * (bottom - top)).rounded(), a * 200 / 255)
    }

    /// The hungry stripes' slide: the board's 34 px cycle in 6 steps every 600 ms.
    public static func stripeOffset(ms: UInt64) -> Double { Double((ms % 600) / 100) * 34 / 6 }
}
