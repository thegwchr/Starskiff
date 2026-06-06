# Starskiff

Starskiff is Feixiao's menu-bar Wi-Fi client for the macOS rtw88 driver.

It is forked from HeliPort's AppKit UI and keeps the same C-facing ClientKit
shape, but the backend talks directly to the rtw88 user-client selectors:
scan, connect, disconnect, state, BSS list, RSSI, debug, and log.

## Current Status

- Builds as `Starskiff.app`.
- Looks for `RTW88PCIDevice` or `RTW88USBDevice` in IORegistry.
- Lists open and WPA2-Personal/CCMP networks from rtw88 scan results.
- Connects and disconnects through the rtw88 kext user client.
- Reports interface name, chip name, firmware version, SSID, BSSID, channel,
  RSSI, and a conservative station-info view to the existing menu UI.

## Implementation Notes

- User interface and password management are implemented with Swift.
- Communication with rtw88 is implemented in `ClientKit/Api.c`.
- Starskiff intentionally preserves the HeliPort API types used by the UI so the
  Swift side remains small and boring.

## Credits

Starskiff is based on HeliPort by OpenIntelWireless contributors.

- [@1Revenger1](https://github.com/1Revenger1) for Keychain password management improvements
- [@Bat.bat](https://github.com/williambj1) for repository management and Sparkle integration
- [@diepeterpan](https://github.com/diepeterpan) for fixing UI artifacts on macOS Sonoma and performance optimizations
- [@DogAndPot](https://github.com/DogAndPot) for initial UI implementation
- [@ErrorErrorError](https://github.com/ErrorErrorError) for UI improvement, Preference Window implementation and more
- [@Goshin](https://github.com/Goshin) for API implementation, Status Menu improvements and more
- [@igorkulman](https://github.com/igorkulman) for code refactoring, password management and more
- [@zxystd](https://github.com/zxystd) for the original Intel Wi-Fi driver/API work
- Everyone who contributed to HeliPort localization files
- Legacy WiFi icons are from <https://icons8.com/>
- Modern WiFi icons are from <https://github.com/framework7io/framework7-icons> (MIT License)
