// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "ClaudeCubeLink",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "ClaudeCubeLink", targets: ["ClaudeCubeLink"]),
        // The screen saver's code: build-saver.sh puts the dylib in build/Claude Cube.saver.
        .library(name: "CubeSaver", type: .dynamic, targets: ["CubeSaver"]),
    ],
    targets: [
        .target(name: "CubeLinkCore"),
        .target(name: "CubeLinkUI", dependencies: ["CubeLinkCore"]),
        .executableTarget(name: "ClaudeCubeLink", dependencies: ["CubeLinkCore", "CubeLinkUI"]),
        .target(name: "CubeSaver", dependencies: ["CubeLinkCore", "CubeLinkUI"]),
        .testTarget(name: "CubeLinkCoreTests", dependencies: ["CubeLinkCore"]),
    ],
    swiftLanguageModes: [.v5]
)
