// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "BSLProtocol",
    platforms: [
        .iOS(.v17),
        .macOS(.v14),
    ],
    products: [
        .library(name: "BSLProtocol", targets: ["BSLProtocol"]),
        .executable(name: "bsl-protocol-check", targets: ["BSLProtocolSanityCheck"]),
    ],
    targets: [
        .target(
            name: "BSLProtocol",
            path: "Sources/BSLProtocol"
        ),
        .executableTarget(
            name: "BSLProtocolSanityCheck",
            dependencies: ["BSLProtocol"],
            path: "Sources/BSLProtocolSanityCheck",
            resources: [
                .copy("Fixtures"),
            ]
        ),
    ]
)
