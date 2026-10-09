import CubeLinkCore
import Foundation

/// Presses play/pause, next and previous for the cube's Volume card, as MediaRemote commands to
/// whichever app owns Now Playing (what Control Center's buttons send).
///
/// macOS (15.4 and later) acts on MediaRemote only for Apple-signed processes, so each press runs
/// a short `osascript` (Apple-signed) that loads the framework, sends the command and stays up
/// half a second so it is delivered. Unlike posting
/// keyboard media-key events, this needs no Accessibility permission, which an ad-hoc signed app
/// would lose on every rebuild. Main queue only.
final class MediaKeys {
    private let log: (String) -> Void
    private var failedLogged = false

    init(log: @escaping (String) -> Void) {
        self.log = log
    }

    /// MRMediaRemoteCommand values.
    private static func command(_ key: MediaKey) -> Int {
        switch key {
        case .playPause: return 2  // kMRTogglePlayPause
        case .next: return 4       // kMRNextTrack
        case .previous: return 5   // kMRPreviousTrack
        }
    }

    func press(_ key: MediaKey) {
        let script = """
            ObjC.import('Foundation');
            $.NSBundle.bundleWithPath('/System/Library/PrivateFrameworks/MediaRemote.framework/').load;
            ObjC.bindFunction('MRMediaRemoteSendCommand', ['bool', ['int', 'id']]);
            const sent = $.MRMediaRemoteSendCommand(\(MediaKeys.command(key)), $());
            // The command goes out asynchronously over XPC: exiting at once drops it.
            delay(0.5);
            sent ? 'ok' : 'refused';
            """
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        p.arguments = ["-l", "JavaScript", "-e", script]
        let out = Pipe()
        p.standardOutput = out
        p.standardError = FileHandle.nullDevice
        p.terminationHandler = { [weak self] proc in
            let reply = String(decoding: out.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
                .trimmingCharacters(in: .whitespacesAndNewlines)
            DispatchQueue.main.async { self?.finished(key, status: proc.terminationStatus, reply: reply) }
        }
        do {
            try p.run()
        } catch {
            log("media \(key): could not start osascript: \(error.localizedDescription)")
        }
    }

    private func finished(_ key: MediaKey, status: Int32, reply: String) {
        if status == 0 && reply == "ok" {
            Trace.log("media", "\(key) sent")
            failedLogged = false
            return
        }
        guard !failedLogged else { return }
        failedLogged = true
        log("media \(key) not sent (osascript \(status), \(reply.isEmpty ? "no reply" : reply))")
    }
}
