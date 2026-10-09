import Foundation

/// Whether the Mac's Now Playing app is playing, for the cube's play/pause button.
///
/// macOS (15.4 and later) answers MediaRemote's now-playing queries only for Apple-signed
/// processes: called from this app they report "not playing" while something plays. So one
/// long-lived `osascript` (Apple-signed) asks MediaRemote twice a second and prints `1`
/// (playing) or `0` (paused, or nothing to play) whenever that changes, and again every 5 s.
/// The repeat lets the child notice this app is gone: its next write fails and it exits.
/// Main queue only.
final class NowPlaying {
    /// Called on the main queue when `playing` changes.
    var onChange: ((Bool?) -> Void)?
    /// Nil until the first answer, and again after the watcher dies, until it answers anew.
    private(set) var playing: Bool?

    private let log: (String) -> Void
    private var process: Process?
    private var stopped = false
    private var buffer = Data()
    private var failures = 0

    private static let script = """
        ObjC.import('Foundation');
        $.NSBundle.bundleWithPath('/System/Library/PrivateFrameworks/MediaRemote.framework/').load;
        const R = $.NSClassFromString('MRNowPlayingRequest');
        const out = $.NSFileHandle.fileHandleWithStandardOutput;
        let last = '', since = 0;
        for (;;) {
          const now = R.localIsPlaying ? '1' : '0';
          if (now !== last || ++since >= 10) {
            out.writeData($(now + '\\n').dataUsingEncoding($.NSUTF8StringEncoding));
            last = now;
            since = 0;
          }
          delay(0.5);
        }
        """

    init(log: @escaping (String) -> Void) {
        self.log = log
    }

    func start() {
        stopped = false
        launch()
    }

    func stop() {
        stopped = true
        process?.terminate()
        process = nil
    }

    private func launch() {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        p.arguments = ["-l", "JavaScript", "-e", NowPlaying.script]
        let out = Pipe()
        p.standardOutput = out
        p.standardError = FileHandle.nullDevice
        out.fileHandleForReading.readabilityHandler = { [weak self] h in
            let data = h.availableData
            DispatchQueue.main.async { self?.received(data) }
        }
        p.terminationHandler = { [weak self] _ in
            out.fileHandleForReading.readabilityHandler = nil
            DispatchQueue.main.async { self?.exited() }
        }
        do {
            try p.run()
            process = p
        } catch {
            log("now playing: could not start osascript: \(error.localizedDescription)")
        }
    }

    private func received(_ data: Data) {
        buffer.append(data)
        while let nl = buffer.firstIndex(of: 0x0A) {
            let line = String(decoding: buffer[buffer.startIndex..<nl], as: UTF8.self)
            buffer.removeSubrange(buffer.startIndex...nl)
            guard line == "0" || line == "1" else { continue }
            failures = 0
            set(line == "1")
        }
    }

    private func exited() {
        process = nil
        buffer.removeAll()
        set(nil)
        guard !stopped else { return }
        failures += 1
        if failures == 3 { log("now playing: the osascript watcher keeps exiting; the cube shows play/pause") }
        // Back off: 2 s, 4 s, ... up to a minute, so a broken MediaRemote does not spin.
        let delay = min(60.0, 2.0 * pow(2.0, Double(min(failures - 1, 5))))
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self, !self.stopped, self.process == nil else { return }
            self.launch()
        }
    }

    private func set(_ value: Bool?) {
        guard value != playing else { return }
        playing = value
        onChange?(value)
    }
}
