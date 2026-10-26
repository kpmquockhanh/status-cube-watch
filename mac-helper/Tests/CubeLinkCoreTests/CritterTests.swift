import Testing
@testable import CubeLinkCore

private let poses = ["idle", "start", "run", "think", "read", "write", "bash", "web", "plan", "delegate", "compact",
                     "wait", "err", "exit", "hungry", "celebrate", "unknown", ""]

@Test func hatCodesDecodeAsShapeTimesEightPlusColour() {
    #expect(Critter.hat(code: nil) == nil)
    #expect(Critter.hat(code: -1) == nil)
    #expect(Critter.hat(code: 104) == nil)
    #expect(Critter.hat(code: 0)! == (0, 0))
    #expect(Critter.hat(code: 42)! == (5, 2))     // beret, yellow
    #expect(Critter.hat(code: 103)! == (12, 7))   // bandana, ink
}

@Test func everyPoseAndHatStaysInBoundsAtEveryMoment() {
    let b = Critter.bounds
    for pose in poses {
        for hat in [nil, 0, 8, 16, 32, 56, 72, 80, 96, 103] {
            for ms in stride(from: UInt64(0), to: 4800, by: 50) {
                let s = Critter.shapes(pose: pose, hat: hat, ms: ms)
                #expect(s.first?.paint == .body)  // the block comes first
                for r in s {
                    #expect(r.x >= b.x && r.y >= b.y && r.x + r.w <= b.x + b.w && r.y + r.h <= b.y + b.h,
                            "\(pose) hat \(hat ?? -1) at \(ms)")
                    #expect(r.alpha > 0 && r.alpha <= 1)
                }
            }
        }
    }
}

@Test func unknownPoseSleeps() {
    #expect(Critter.shapes(pose: "nope", hat: 3, ms: 700) == Critter.shapes(pose: "idle", hat: 3, ms: 700))
}

@Test func runningStepsAndBobs() {
    let a = Critter.shapes(pose: "run", hat: nil, ms: 0), b = Critter.shapes(pose: "run", hat: nil, ms: 250)
    #expect(a != b)
    #expect(a[0].y == 0 && b[0].y == -0.5)  // the bob
    #expect(Critter.shapes(pose: "run", hat: nil, ms: 6000) == a)  // the 0.5 s step and the 1.2 s dots meet again
}

@Test func workingHatsAnimateAndOthersKeepTheirStill() {
    // propeller: 4 frames of 120 ms while working
    let still = Critter.shapes(pose: "wait", hat: 9 * 8, ms: 0)
    #expect(Critter.shapes(pose: "wait", hat: 9 * 8, ms: 120).filter { if case .rgb = $0.paint { true } else { false } }
        == still.filter { if case .rgb = $0.paint { true } else { false } })
    let spin0 = Critter.shapes(pose: "plan", hat: 9 * 8, ms: 0), spin1 = Critter.shapes(pose: "plan", hat: 9 * 8, ms: 120)
    #expect(spin0.filter { if case .rgb = $0.paint { true } else { false } }
        != spin1.filter { if case .rgb = $0.paint { true } else { false } })
}

@Test func hatsAreDrawnInTheirColoursAndFadeOnAnExitedAgent() {
    let teal = Critter.hatColors[0].0
    #expect(Critter.shapes(pose: "wait", hat: 0).contains(Critter.Rect(x: 4, y: -4, w: 5, h: 1, paint: .rgb(teal))))
    #expect(Critter.shapes(pose: "exit", hat: 0).contains(Critter.Rect(x: 4, y: -4, w: 5, h: 1, paint: .rgb(teal), alpha: 0.45)))
    #expect(!Critter.animates(pose: "exit") && Critter.animates(pose: "idle"))
}

@Test func runGroundWaveAndSpecks() {
    // resting part of the cycle: every dot at the base
    #expect(TileGround.dotAlpha(dx: 50, dy: 50, w: 140, h: 120, ms: 2000) == 38.0 / 255)
    // the crest passes the middle mid-sweep
    #expect(TileGround.dotAlpha(dx: 70, dy: 60, w: 140, h: 120, ms: 900) > 0.5)
    let p0 = TileGround.particle(0, w: 140, h: 120, ms: 0), p1 = TileGround.particle(0, w: 140, h: 120, ms: 1500)
    #expect(p0.alpha == 0 && p1.alpha > 0.7 && p1.y < 120 - 8)
    #expect(TileGround.particle(4, w: 140, h: 120, ms: 0).alpha == 0)
    #expect(TileGround.stripeOffset(ms: 0) == 0 && TileGround.stripeOffset(ms: 599) == 34.0 * 5 / 6)
}
