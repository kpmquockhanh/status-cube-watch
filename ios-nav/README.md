# CubeNav (iPhone)

SwiftUI app that searches places, gets motorbike routes from https://nav.devxdev.site, follows the ride in the background and streams map frames to the cube over Bluetooth. Pure logic is in the local package `NavCore` (`cd NavCore && swift test`).

## Build

```sh
brew install xcodegen
cd ios-nav
xcodegen && open CubeNav.xcodeproj
```

Needs Xcode 16 or newer. On the iPhone, turn on Developer Mode (Settings > Privacy & Security > Developer Mode, then restart). Pick your iPhone and your team under Signing, then Run.

With a free Apple ID the bundle id `com.claude-cube.nav` may already be taken: change `PRODUCT_BUNDLE_IDENTIFIER` in `project.yml` to something unique (for example `com.yourname.cubenav`), run `xcodegen` again and Run.

## Weekly rebuild

With a free Apple ID the provisioning profile expires after 7 days: rebuild and run from Xcode once a week to renew it.

## The key

The nav server API key is not committed and never stored in the repo. After the first install open the gear icon on the main screen (Settings), enter the server URL (default `https://nav.devxdev.site`) and the key, then Save. The key is kept in the iPhone Keychain. The main screen warns when the app will stop working in under 2 days (red under 12 hours).

## Using it

1. Open the app. The Cube card says whether the cube is linked and, if not, what to do (Bluetooth off or not allowed, pairing code, Pair new device in the cube's portal, forget Claude Cube in iOS Settings > Bluetooth, firmware too old). Tap Try again after fixing it; the app also retries each time it comes to the foreground. After replacing the cube, use Settings > Forget cube.
2. Search a place (or tap a recent one). The preview draws the server's route with distance and time. Start downloads the map for the route, then the ride screen takes over; allow location "Always" so it keeps going with the phone locked. The download carries on if you pocket the phone; going back from the preview cancels it. With no connection, the preview offers "Start saved route (offline)" when the last ride went to the same place (within about 100 m).
3. Stop sends the end of the ride to the cube and turns off background location: the GPS keeps running only while the app is open (for the map and the next route), and stops when the app goes to the background.

## On-bike checklist

- Built from Xcode this week; key entered; Bluetooth on; cube connected on the main screen.
- Start with mobile data: the whole map is downloaded first, so a lost signal later only disables rerouting (after a reroute, the map of the new part fills in as the signal allows).
- Lock the phone, ride 500 m: the cube map follows.
- Signal lost and off the route: the cube shows OFF ROUTE, NO SIGNAL with an arrow back; it reroutes when the signal returns.
- Stop at the end, and check the cube leaves the map.
