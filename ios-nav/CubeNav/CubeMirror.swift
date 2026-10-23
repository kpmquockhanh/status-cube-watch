import SwiftUI
import NavCore

/// What the cube is showing right now, drawn from the same `NavScene` the phone sends it, with
/// the firmware's layout (`uiNav` in firmware/src/ui.cpp): streets by class, the route on top, the
/// rider arrow at (120, 200), the turn banner and the street line. It works in the cube's own
/// 240x280 pixel space and scales to fit.
struct CubeMirror: View {
    let scene: NavScene
    var phoneLost = false

    private static let size = CGSize(width: 240, height: 280)

    var body: some View {
        GeometryReader { geo in
            let k = min(geo.size.width / Self.size.width, geo.size.height / Self.size.height)
            screen
                .frame(width: Self.size.width, height: Self.size.height)
                .scaleEffect(k, anchor: .topLeading)
                .frame(width: Self.size.width * k, height: Self.size.height * k)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .aspectRatio(Self.size, contentMode: .fit)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(accessibilitySummary)
    }

    private var screen: some View {
        ZStack(alignment: .top) {
            Color.black
            if scene.state == .arrived {
                arrived
            } else {
                Canvas { ctx, _ in drawMap(&ctx) }
                banner
                bottomLine
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 38, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 38, style: .continuous).strokeBorder(Color.white.opacity(0.12), lineWidth: 2))
    }

    // MARK: map

    private static let streetStyle: [(width: CGFloat, color: Color)] = [
        (7, Color(hex: 0x59606F)), (5, Color(hex: 0x4A5263)), (3, Color(hex: 0x3A4152)), (2, Color(hex: 0x2A3040)),
    ]

    private func drawMap(_ ctx: inout GraphicsContext) {
        for cls in stride(from: 3, through: 0, by: -1) {
            let style = Self.streetStyle[cls]
            for s in scene.streets where Int(s.cls) == cls {
                ctx.stroke(path(s.points), with: .color(style.color),
                           style: StrokeStyle(lineWidth: style.width, lineCap: .round, lineJoin: .round))
            }
        }
        if scene.route.count >= 2 {
            let live = scene.state == .navigating
            ctx.stroke(path(scene.route), with: .color(live ? Theme.route : Color(hex: 0x7C8598)),
                       style: StrokeStyle(lineWidth: 12, lineCap: .round, lineJoin: .round))
        }
        var rider = Path()
        rider.move(to: CGPoint(x: 120, y: 180))
        rider.addLine(to: CGPoint(x: 136, y: 220))
        rider.addLine(to: CGPoint(x: 120, y: 210))
        rider.addLine(to: CGPoint(x: 104, y: 220))
        rider.closeSubpath()
        ctx.stroke(rider, with: .color(.black), style: StrokeStyle(lineWidth: 6, lineJoin: .round))
        ctx.fill(rider, with: .color(scene.gpsWeak ? Color(hex: 0x7C8598) : .white))
    }

    private func path(_ pts: [ScreenPoint]) -> Path {
        var p = Path()
        guard let first = pts.first else { return p }
        p.move(to: CGPoint(x: CGFloat(first.x), y: CGFloat(first.y)))
        for q in pts.dropFirst() { p.addLine(to: CGPoint(x: CGFloat(q.x), y: CGFloat(q.y))) }
        return p
    }

    // MARK: banner and bottom line

    private var banner: some View {
        HStack(spacing: 10) {
            TurnGlyph(turn: scene.turn, exit: scene.exit, dir: scene.dir)
                .font(.system(size: 30, weight: .bold))
                .foregroundStyle(.white)
                .frame(width: 44, height: 44)
            VStack(alignment: .leading, spacing: 1) {
                if scene.state == .rerouting {
                    Text("Rerouting").font(.system(size: 22, weight: .bold))
                } else {
                    Text(scene.dist).font(Theme.number(28))
                }
                if scene.state == .offRouteNoSignal {
                    Text("OFF ROUTE · NO SIGNAL").font(.system(size: 9, weight: .bold)).foregroundStyle(Theme.warn)
                }
            }
            .foregroundStyle(.white)
            Spacer(minLength: 0)
        }
        .padding(.leading, 16).padding(.trailing, 8)
        .frame(height: 62)
        .background(Color(hex: 0x0B0D12))
        .overlay(alignment: .topTrailing) {
            if phoneLost {
                Text("NO PHONE").font(.system(size: 9, weight: .bold)).foregroundStyle(Theme.warn)
                    .padding(.top, 8).padding(.trailing, 10)
            }
        }
    }

    private var bottomLine: some View {
        VStack {
            Spacer()
            HStack(spacing: 6) {
                Text(scene.street).lineLimit(1).truncationMode(.tail).foregroundStyle(.white)
                if !scene.eta.isEmpty { Text(scene.eta).foregroundStyle(Color(hex: 0x7C8598)).fixedSize() }
            }
            .font(.system(size: 15, weight: .bold))
            .padding(.horizontal, 14)
            .frame(maxWidth: .infinity, minHeight: 34)
            .background(Color.black)
        }
    }

    private var arrived: some View {
        VStack(spacing: 10) {
            Spacer()
            Image(systemName: "checkmark.circle")
                .font(.system(size: 72, weight: .semibold))
                .foregroundStyle(Theme.accent)
            Text("Arrived").font(.system(size: 28, weight: .bold)).foregroundStyle(.white)
            Text(scene.street).font(.system(size: 15, weight: .bold)).foregroundStyle(Color(hex: 0x7C8598))
                .lineLimit(1).padding(.horizontal, 16)
            Spacer()
        }
    }

    private var accessibilitySummary: String {
        switch scene.state {
        case .arrived: return "Cube shows: arrived"
        case .rerouting: return "Cube shows: rerouting"
        case .offRouteNoSignal: return "Cube shows: off route, no signal, \(scene.dist) back to the route"
        default: return "Cube shows: \(TurnGlyph.words(scene.turn)) in \(scene.dist), \(scene.street)"
        }
    }
}

/// The turn as an SF Symbol, matching the cube's arrow set.
struct TurnGlyph: View {
    let turn: NavTurn
    var exit: UInt8 = 0
    var dir: UInt8 = 0

    var body: some View {
        switch turn {
        case .roundabout:
            ZStack {
                Image(systemName: "circle").font(.system(size: 30, weight: .bold))
                if exit > 0 { Text("\(exit)").font(.system(size: 14, weight: .heavy, design: .rounded)) }
            }
        case .rejoin:
            Image(systemName: "location.north.fill")
                .rotationEffect(.degrees(Double(dir) * 360 / 256))
        default:
            Image(systemName: Self.symbol(turn))
        }
    }

    static func symbol(_ t: NavTurn) -> String {
        switch t {
        case .straight: return "arrow.up"
        case .slightLeft, .keepLeft: return "arrow.up.left"
        case .left: return "arrow.turn.up.left"
        case .sharpLeft: return "arrow.down.left"
        case .slightRight, .keepRight: return "arrow.up.right"
        case .right: return "arrow.turn.up.right"
        case .sharpRight: return "arrow.down.right"
        case .uTurn: return "arrow.uturn.down"
        case .roundabout: return "circle"
        case .arrive: return "flag.checkered"
        case .rejoin: return "location.north.fill"
        }
    }

    static func words(_ t: NavTurn) -> String {
        switch t {
        case .straight: return "continue straight"
        case .slightLeft: return "bear left"
        case .left: return "turn left"
        case .sharpLeft: return "sharp left"
        case .slightRight: return "bear right"
        case .right: return "turn right"
        case .sharpRight: return "sharp right"
        case .uTurn: return "make a U-turn"
        case .keepLeft: return "keep left"
        case .keepRight: return "keep right"
        case .roundabout: return "take the roundabout"
        case .arrive: return "arrive"
        case .rejoin: return "head back to the route"
        }
    }
}
