import Foundation

/// Verbose diagnostics, off by default. Enable with `CUBE_TRACE=1` or `--trace`.
/// Lines go to stderr (the LaunchAgent log) with a timestamp and a category tag.
public enum Trace {
    public nonisolated(unsafe) static var enabled = false

    private static let queue = DispatchQueue(label: "cubelink.trace")
    private static let stamp = ISO8601DateFormatter()

    public static func configure(env: [String: String], args: [String]) {
        let v = env["CUBE_TRACE"] ?? ""
        enabled = args.contains("--trace") || (!v.isEmpty && v != "0" && v.lowercased() != "false")
    }

    /// The message is an autoclosure, so nothing is built while tracing is off.
    public static func log(_ tag: String, _ message: @autoclosure () -> String) {
        guard enabled else { return }
        let line = "\(stamp.string(from: Date())) [trace:\(tag)] \(message())\n"
        queue.async { FileHandle.standardError.write(Data(line.utf8)) }
    }

    /// An always-on line (the app's `log`). Same stamp and same serial queue as trace lines, so the
    /// two never land out of order; synchronous, so a line logged right before `exit` is not lost.
    public static func info(_ message: String) {
        let line = "\(stamp.string(from: Date())) \(message)\n"
        queue.sync { FileHandle.standardError.write(Data(line.utf8)) }
    }
}
