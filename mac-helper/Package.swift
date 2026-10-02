// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "ClaudeCubeLink",
    platforms: [.macOS(.v13)],
    targets: [
        .target(name: "CubeLinkCore"),
        .executableTarget(name: "ClaudeCubeLink", dependencies: ["CubeLinkCore"]),
        .testTarget(name: "CubeLinkCoreTests", dependencies: ["CubeLinkCore"]),
    ],
    swiftLanguageModes: [.v5]
)
