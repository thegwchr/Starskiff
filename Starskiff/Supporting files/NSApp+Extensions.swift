//
//  NSApp+Extensions.swift
//  Starskiff
//
//  Created by Bat.bat on 29/7/2024.
//  Copyright © 2024 Feixiao. All rights reserved.
//

import Cocoa

extension NSApplication {
    @available(macOS 10.15, *)
    func restartApp() {
        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true

        NSWorkspace.shared.openApplication(at: Bundle.main.bundleURL,
                                           configuration: config,
                                           completionHandler: { _, error in
            if let error {
                Log.error("Failed to restart the app: \(error)")
            } else {
                DispatchQueue.main.async {
                    NSApp.terminate(nil)
                }
            }
        })
    }
}
