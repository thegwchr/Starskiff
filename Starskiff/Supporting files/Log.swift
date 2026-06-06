//
//  Log.swift
//  Starskiff
//
//  Created by Igor Kulman on 22/06/2020.
//  Copyright © 2020 Feixiao. All rights reserved.
//

/*
 * This program and the accompanying materials are licensed and made available
 * under the terms and conditions of the The 3-Clause BSD License
 * which accompanies this distribution. The full text of the license may be found at
 * https://opensource.org/licenses/BSD-3-Clause
 */

import Foundation
import os.log

final class Log {
    static func debug(_ message: String) {
        if #available(OSX 11.0, *) {
            Logger.starskiff.info("DEBUG: \(message, privacy: .public)")
        } else {
            os_log("%{public}@", log: .starskiff, type: .info, "DEBUG: " + message)
        }
    }

    static func error(_ message: String) {
        if #available(OSX 11.0, *) {
            Logger.starskiff.error("ERROR: \(message, privacy: .public)")
        } else {
            os_log("%{public}@", log: .starskiff, type: .error, "ERROR: " + message)
        }
    }
}

@available(OSX 11.0, *)
extension Logger {
    static let starskiff = Logger(subsystem: Bundle.main.bundleIdentifier!, category: "Starskiff")
}

extension OSLog {
    static let starskiff = OSLog(subsystem: Bundle.main.bundleIdentifier!, category: "Starskiff")
}
