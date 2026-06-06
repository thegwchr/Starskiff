//
//  BugReporter.swift
//  Starskiff
//
//  Created by Erik Bautista on 7/26/20.
//  Copyright © 2020 Feixiao. All rights reserved.
//

/*
 * This program and the accompanying materials are licensed and made available
 * under the terms and conditions of the The 3-Clause BSD License
 * which accompanies this distribution. The full text of the license may be found at
 * https://opensource.org/licenses/BSD-3-Clause
 */

import Cocoa
import OSLog
import IOKit

class BugReporter {

    private static let openPanel: NSOpenPanel = {
        let openPanel = NSOpenPanel()

        openPanel.title = NSLocalizedString("Choose a folder to output the bug report")
        openPanel.message = NSLocalizedString("The bug report will be generated in the seleted folder")
        openPanel.showsResizeIndicator = true
        openPanel.canChooseDirectories = true
        openPanel.canChooseFiles = false
        openPanel.allowsMultipleSelection = false
        openPanel.canCreateDirectories = true

        NSApplication.shared.activate(ignoringOtherApps: true)

        return openPanel
    }()

    private class func generateStarskiffLog() -> String {

        // MARK: Starskiff log

        let appIdentifier = Bundle.main.bundleIdentifier!

        if #available(OSX 10.15, *) {
            do {
                let logStore = try OSLogStore.local()
                let lastBoot = logStore.position(timeIntervalSinceLatestBoot: 0)
                let matchingPredicate = NSPredicate(format: "subsystem == '\(appIdentifier)'")
                let enumerator = try logStore.getEntries(with: [],
                                                         at: lastBoot,
                                                         matching: matchingPredicate)
                let allEntries = Array(enumerator)
                let osLogEntryLogObjects = allEntries.compactMap { $0 as? OSLogEntryLog }
                var entryStr = ""
                for item in osLogEntryLogObjects where item.subsystem == appIdentifier {
                    entryStr += "\n\(item.date);    \(item.subsystem);    \(item.category);    \(item.composedMessage)"
                }
                return entryStr
            } catch {
                Log.error("Could not generate bug report \(error)")
                return .heliportCouldNotGetLogs
            }
        } else {
            let appLogCommand = ["show", "--predicate",
                                      "(subsystem == '\(appIdentifier)')", "--info", "--last", "boot"]
            let appLog = Commands.execute(executablePath: .log, args: appLogCommand)
            if let stringVal = appLog.0, appLog.1 == 0 {
                return stringVal
            } else {
                return .scriptFailed
            }
        }
    }

    private class func generateItlwmLog() -> String {
        var response: String?

        if KextInfo("as.lvs1974.DebugEnhancer").kextDidLoad() {
            // msgbuf size is sufficient, collect dmesg logs
            response = NSAppleScript(source:
                                     // swiftlint:disable line_length
                                     """
                                     do shell script \"sudo dmesg | grep -E \\"rtw88|Airport|IO80211|EAPOL\\"\" with administrator privileges
                                     """)!.executeAndReturnError(nil).stringValue
                                     // swiftlint:enable line_length
        } else {
            response = .msgbufInsufficient
        }

        return response ?? .scriptFailed
    }

    public class func generateBugReport() {
        let appVersion = Bundle.main.infoDictionary?["CFBundleShortVersionString"] ?? "Unknown"
        let appBuildVer = Bundle.main.infoDictionary?["CFBundleVersion"] ?? "Unknown"

        let appLog = generateStarskiffLog()

        if appLog == .heliportCouldNotGetLogs || appLog == .scriptFailed {
            DispatchQueue.main.async {
                let alert = CriticalAlert(
                    message: NSLocalizedString("Error occurred while generating bug report."),
                    informativeText: appLog == .heliportCouldNotGetLogs ?
                    NSLocalizedString("Could not generate report for Starskiff.") :
                    NSLocalizedString("Command failed to fetch logs for Starskiff."),
                    options: [NSLocalizedString("Dismiss")],
                    errorText: appLog
                )
                alert.show()
            }
            return
        }

        // MARK: rtw88 log

        var drv_info = ioctl_driver_info()
        _ = ioctl_get(Int32(IOCTL_80211_DRIVER_INFO.rawValue), &drv_info, MemoryLayout<ioctl_driver_info>.size)
        var rtw88Ver = String(cCharArray: drv_info.driver_version)
        var rtw88FwVer = String(cCharArray: drv_info.fw_version)
        if rtw88Ver.isEmpty { rtw88Ver = "Unknown" }
        if rtw88FwVer.isEmpty { rtw88FwVer = "Unknown" }

        let rtw88Log = generateItlwmLog()

        if rtw88Log == .msgbufInsufficient || rtw88Log == .scriptFailed {
            DispatchQueue.main.async {
                let alert = CriticalAlert(
                    message: NSLocalizedString("Error occurred while generating bug report."),
                    informativeText: rtw88Log == .msgbufInsufficient ?
                    NSLocalizedString("Make sure you have installed `DebugEnhancer.kext`" +
                                      " before collecting logs for rtw88.") :
                    NSLocalizedString("Could not read logs for `rtw88`." +
                                      " Make sure you allow `Starskiff` to read logs when prompted."),
                    options: [NSLocalizedString("Dismiss"), NSLocalizedString("Open Documentation")],
                    helpAnchor: .dmesgHelpURL,
                    errorText: rtw88Log
                )

                if alert.show() == .alertSecondButtonReturn {
                    NSWorkspace.shared.open(URL(string: .dmesgHelpURL)!)
                }
            }
            return
        }

        // MARK: Get rtw88 name if loaded (rtw88 or rtw88x)

        let kextstatCommand = ["-c", "kextstat"]
        let rtw88Loaded = Commands.execute(executablePath: .shell, args: kextstatCommand)
        var rtw88Name: String?
        if let regex = try? NSRegularExpression.init(pattern: "\\b(rtw88\\w*)\\b", options: []), rtw88Loaded.0 != nil {
            let firstMatch = regex.firstMatch(in: rtw88Loaded.0!,
                                            options: [],
                                            range: NSRange(location: 0, length: rtw88Loaded.0!.count))
            if let range = firstMatch?.range(at: 1) {
                if let swiftRange = Range(range, in: rtw88Loaded.0!) {
                    rtw88Name = String(rtw88Loaded.0![swiftRange])
                }
            }
        }

        // MARK: Output String

        let date = Date()
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss.SSSS"
        let dateRan = "Time ran: \(formatter.string(from: date))"
        let osVersion = ProcessInfo().operatingSystemVersionString
        let appOutput = """
                        \(appLog)

                        \(dateRan)
                        Starskiff Version: \(appVersion) (Build \(appBuildVer))

                        macOS \(osVersion)
                        """
        let rtw88Output = """
                          \(rtw88Log)

                          \(dateRan)
                          \(rtw88Name != nil ?  "\(rtw88Name!) loaded version: \(rtw88Ver) (Firmware: \(rtw88FwVer))" :
                                "Kext not loaded")

                          macOS \(osVersion)
                          """

        DispatchQueue.main.async {
            openPanel.begin { (result) in
                var folderUrl: URL?
                if result == NSApplication.ModalResponse.OK {
                    folderUrl = openPanel.url
                }

                // Back to background
                DispatchQueue.global().async {
                    guard folderUrl != nil else {
                        Log.error("Could not get path to store bug report.")
                        DispatchQueue.main.async {
                            let alert = CriticalAlert(
                                message: NSLocalizedString("Could not get path to generate bug report."),
                                options: ["Dismiss"]
                            )
                            alert.show()
                        }
                        return
                    }

                    let reportDirName = "bugreport_\(UInt16.random(in: UInt16.min...UInt16.max))"
                    let reportDirUrl = folderUrl!.appendingPathComponent(reportDirName, isDirectory: true)

                    // MARK: Write to files

                    do {
                        try FileManager.default.createDirectory(at: reportDirUrl,
                                                                withIntermediateDirectories: true,
                                                                attributes: nil)
                        let starskiffFile = reportDirUrl.appendingPathComponent("Starskiff_logs.log")
                        let rtw88File = reportDirUrl.appendingPathComponent("\(rtw88Name ?? "rtw88")_logs.log")
                        try appOutput.write(to: starskiffFile, atomically: true, encoding: .utf8)
                        try rtw88Output.write(to: rtw88File, atomically: true, encoding: .utf8)
                    } catch {
                        Log.error("\(error)")
                        return
                    }

                    // MARK: Zip file

                    let zipName = reportDirName + ".zip"
                    let zipCommand = ["-c", "cd \(folderUrl!.path) && " +
                                            "zip -r -X -m \(zipName) \(reportDirName)"]
                    let outputExitCode = Commands.execute(executablePath: .shell, args: zipCommand).1
                    guard outputExitCode == 0 else {
                        Log.error("Could not create zip file: Exit code: \(outputExitCode)")
                        DispatchQueue.main.async {
                            let alert = CriticalAlert(
                                message: NSLocalizedString("Could not create zip file for generated logs."),
                                options: [NSLocalizedString("Dismiss")]
                            )
                            alert.show()
                        }
                        return
                    }

                    // MARK: Select zip file

                    NSWorkspace.shared.selectFile("\(folderUrl!.path)/\(zipName)",
                                                  inFileViewerRootedAtPath: folderUrl!.path)
                }
            }
        }
    }
}

private extension String {

    // MARK: Starskiff Generation errors

    static let heliportCouldNotGetLogs = "HELIPORT-OSLOGSTORE"

    // MARK: ITLWM Generation errors

    static let msgbufInsufficient = "MSGBUF-INSUFFICIENT"
    static let scriptFailed = "SCRIPT-FAILED"

    // MARK: DOC URL
    static let dmesgHelpURL = "https://docs.oiw.workers.dev/rtw88/Troubleshooting.html#runtime-logs"
}
