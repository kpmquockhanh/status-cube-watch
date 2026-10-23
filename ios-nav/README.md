# CubeNav (iPhone)

SwiftUI app that searches places, gets motorbike routes from https://nav.devxdev.site, follows the ride in the background and streams map frames to the cube over Bluetooth. Pure logic is in the local package `NavCore` (`cd NavCore && swift test`).

## Build

```sh
brew install xcodegen
cd ios-nav
xcodegen && open CubeNav.xcodeproj
```

Pick your iPhone and your team under Signing, then Run.

## Weekly rebuild

With a free Apple ID the provisioning profile expires after 7 days: rebuild and run from Xcode once a week to renew it.

## The key

The nav server API key is not committed; it is set in the app (see the later tasks) and never stored in the repo.
