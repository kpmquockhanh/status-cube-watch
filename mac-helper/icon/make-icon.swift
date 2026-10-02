// Draws AppIcon.icns: swift icon/make-icon.swift  (run by build-app.sh only when the .icns is missing)
import AppKit

func render(_ px: Int) -> Data {
    let s = CGFloat(px)
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let ctx = NSGraphicsContext.current!.cgContext
    let accent = NSColor(red: 0.851, green: 0.467, blue: 0.341, alpha: 1)  // Claude orange

    // macOS icon grid: the squircle fills 824/1024 of the canvas.
    let body = CGRect(x: s * 0.098, y: s * 0.098, width: s * 0.804, height: s * 0.804)
    let path = NSBezierPath(roundedRect: body, xRadius: s * 0.18, yRadius: s * 0.18)
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -s * 0.012), blur: s * 0.03,
                  color: NSColor.black.withAlphaComponent(0.35).cgColor)
    NSColor.black.setFill(); path.fill()
    ctx.restoreGState()
    path.addClip()
    NSGradient(starting: NSColor(white: 0.20, alpha: 1), ending: NSColor(white: 0.07, alpha: 1))!
        .draw(in: body, angle: -90)

    let c = CGPoint(x: s / 2, y: s / 2)
    let r = s * 0.29
    // Track + usage arc (about 72%, starting at 12 o'clock, clockwise).
    let track = NSBezierPath(); track.lineWidth = s * 0.055; track.lineCapStyle = .round
    track.appendArc(withCenter: c, radius: r, startAngle: 0, endAngle: 360)
    NSColor(white: 1, alpha: 0.12).setStroke(); track.stroke()
    let arc = NSBezierPath(); arc.lineWidth = s * 0.055; arc.lineCapStyle = .round
    arc.appendArc(withCenter: c, radius: r, startAngle: 90, endAngle: 90 - 360 * 0.72, clockwise: true)
    accent.setStroke(); arc.stroke()

    // Isometric cube inside the ring.
    let k = s * 0.165
    func pt(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: c.x + x * k, y: c.y + y * k) }
    let h: CGFloat = 0.866
    let top = [pt(0, 1.0), pt(h, 0.5), pt(0, 0), pt(-h, 0.5)]
    let left = [pt(-h, 0.5), pt(0, 0), pt(0, -1.0), pt(-h, -0.5)]
    let right = [pt(0, 0), pt(h, 0.5), pt(h, -0.5), pt(0, -1.0)]
    for (face, shade) in [(top, 1.0), (left, 0.62), (right, 0.8)] as [([CGPoint], CGFloat)] {
        let p = NSBezierPath(); p.move(to: face[0]); face.dropFirst().forEach { p.line(to: $0) }; p.close()
        p.lineJoinStyle = .round
        NSColor(white: shade * 0.96, alpha: 1).setFill(); p.fill()
    }
    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

let set = URL(fileURLWithPath: CommandLine.arguments[1])
try? FileManager.default.createDirectory(at: set, withIntermediateDirectories: true)
for (name, px) in [("16x16", 16), ("16x16@2x", 32), ("32x32", 32), ("32x32@2x", 64), ("128x128", 128),
                   ("128x128@2x", 256), ("256x256", 256), ("256x256@2x", 512), ("512x512", 512),
                   ("512x512@2x", 1024)] {
    try render(px).write(to: set.appendingPathComponent("icon_\(name).png"))
}
