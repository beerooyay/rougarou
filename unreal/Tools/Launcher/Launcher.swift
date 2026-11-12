import AppKit
import Darwin

func fail(_ message: String) -> Never {
    NSApplication.shared.setActivationPolicy(.accessory)
    NSApplication.shared.activate()
    let alert = NSAlert()
    alert.alertStyle = .critical
    alert.messageText = "Rougarou couldn’t open"
    alert.informativeText = message
    alert.runModal()
    exit(1)
}

let project = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Desktop/rougarou/unreal/Rougarou.uproject")
let engine = "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
let log = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Logs/RougarouLauncher.log")
guard FileManager.default.isExecutableFile(atPath: engine) else {
    fail("The local Unreal Engine 5.8 installation is required.")
}
do {
    let file = try FileHandle(forReadingFrom: project)
    try file.close()
} catch {
    fail("Rougarou needs access to the game project at \(project.path). \(error.localizedDescription)")
}

// Avoid the macOS native fullscreen transition during UE startup.
let screen = NSScreen.main ?? NSScreen.screens.first
let visible = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
let full = screen?.frame ?? visible
let width = max(640, Int(visible.width))
let height = max(480, Int(visible.height) - 32)
let top = Int(full.maxY - visible.maxY) + 32
let arguments = [
    engine, project.path, "-game", "-windowed", "-ResX=\(width)", "-ResY=\(height)", "-WinX=\(Int(visible.minX))", "-WinY=\(top)",
    "-nop4", "-noanalytics", "-latestplayer", "-shoulderv2", "-abslog=\(log.path)"
] + CommandLine.arguments.dropFirst().filter { !$0.hasPrefix("-psn_") }
var pointers = arguments.map { strdup($0) } + [nil]
_ = execv(engine, &pointers)
let error = String(cString: strerror(errno))
pointers.forEach { free($0) }
fail(error)
