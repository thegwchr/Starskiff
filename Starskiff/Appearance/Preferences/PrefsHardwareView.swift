//
//  PrefsHardwareView.swift
//  Starskiff
//
//  Copyright (c) 2026 OpenIntelWireless & thegwchr. All rights reserved.
//

import Cocoa

final class PrefsHardwareView: NSView {

    private let gridView = NSGridView()
    private let refreshButton = NSButton(title: .refresh, target: nil, action: nil)

    private let interfaceValue = NSTextField(labelWithString: .unknown)
    private let chipValue = NSTextField(labelWithString: .unknown)
    private let firmwareValue = NSTextField(labelWithString: .unknown)
    private let macValue = NSTextField(labelWithString: .unknown)
    private let stateValue = NSTextField(labelWithString: .unknown)
    private let ssidValue = NSTextField(labelWithString: .notConnected)
    private let bssidValue = NSTextField(labelWithString: .notConnected)
    private let channelValue = NSTextField(labelWithString: .unknown)
    private let rssiValue = NSTextField(labelWithString: .unknown)
    private let trafficValue = NSTextField(labelWithString: .unknown)

    convenience init() {
        self.init(frame: .zero)
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)

        refreshButton.target = self
        refreshButton.action = #selector(refresh)

        setupGrid()
        addSubview(gridView)
        addSubview(refreshButton)
        setupConstraints()
        refresh()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setupGrid() {
        gridView.rowSpacing = 8
        gridView.columnSpacing = 12

        addRow(.interface, interfaceValue)
        addRow(.chipset, chipValue)
        addRow(.firmware, firmwareValue)
        addRow(.macAddress, macValue)
        addRow(.state, stateValue)
        addRow(.ssid, ssidValue)
        addRow(.bssid, bssidValue)
        addRow(.channel, channelValue)
        addRow(.rssi, rssiValue)
        addRow(.traffic, trafficValue)
    }

    private func addRow(_ title: String, _ value: NSTextField) {
        let label = NSTextField(labelWithString: title)
        label.alignment = .right
        value.lineBreakMode = .byTruncatingMiddle
        value.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        gridView.addRow(with: [label, value])
    }

    private func setupConstraints() {
        subviews.forEach { $0.translatesAutoresizingMaskIntoConstraints = false }

        let inset: CGFloat = 20
        NSLayoutConstraint.activate([
            gridView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: inset),
            gridView.topAnchor.constraint(equalTo: topAnchor, constant: inset),
            gridView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -inset),

            refreshButton.topAnchor.constraint(equalTo: gridView.bottomAnchor, constant: 16),
            refreshButton.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -inset),
            refreshButton.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -inset)
        ])
    }

    @objc private func refresh() {
        var info = hardware_info_t()
        guard get_hardware_info(&info) else {
            setUnavailable()
            return
        }

        interfaceValue.stringValue = nonEmpty(String(cCharArray: info.interface_name))
        chipValue.stringValue = nonEmpty(String(cCharArray: info.chip_name))
        firmwareValue.stringValue = "\(info.fw_version).\(info.fw_sub_version)"
        macValue.stringValue = formatAddress(info.mac_addr)
        stateValue.stringValue = stateDescription(info.state)

        let ssid = String(cCharArray: info.ssid)
        ssidValue.stringValue = ssid.isEmpty ? .notConnected : ssid
        bssidValue.stringValue = formatAddress(info.bssid)
        channelValue.stringValue = info.channel == 0 ? .unknown : String(info.channel)
        rssiValue.stringValue = info.rssi <= -100 ? .unknown : "\(info.rssi) dBm"
        trafficValue.stringValue = "\(formatBytes(info.rx_byte_count)) down / \(formatBytes(info.tx_byte_count)) up"
    }

    private func setUnavailable() {
        [interfaceValue, chipValue, firmwareValue, macValue, stateValue, ssidValue,
         bssidValue, channelValue, rssiValue, trafficValue].forEach {
            $0.stringValue = .unavailable
        }
    }

    private func nonEmpty(_ value: String) -> String {
        return value.isEmpty ? .unknown : value
    }

    private func formatAddress<T>(_ address: T) -> String {
        let bytes = withUnsafeBytes(of: address) { Array($0.prefix(6)) }
        guard bytes.contains(where: { $0 != 0 }) else {
            return .unknown
        }
        return bytes.map { String(format: "%02x", $0) }.joined(separator: ":")
    }

    private func stateDescription(_ state: UInt32) -> String {
        switch state {
        case 0: return NSLocalizedString("Idle")
        case 1: return NSLocalizedString("Scanning")
        case 2: return NSLocalizedString("Authenticating")
        case 3: return NSLocalizedString("Associating")
        case 4: return NSLocalizedString("Handshaking")
        case 5: return NSLocalizedString("Connected")
        case 6: return NSLocalizedString("Disconnecting")
        default: return .unknown
        }
    }

    private func formatBytes(_ value: UInt32) -> String {
        return ByteCountFormatter.string(fromByteCount: Int64(value), countStyle: .binary)
    }
}

private extension String {
    static let refresh = NSLocalizedString("Refresh")
    static let interface = NSLocalizedString("Interface:")
    static let chipset = NSLocalizedString("Chipset:")
    static let firmware = NSLocalizedString("Firmware:")
    static let macAddress = NSLocalizedString("MAC Address:")
    static let state = NSLocalizedString("State:")
    static let ssid = NSLocalizedString("SSID:")
    static let bssid = NSLocalizedString("BSSID:")
    static let channel = NSLocalizedString("Channel:")
    static let rssi = NSLocalizedString("RSSI:")
    static let traffic = NSLocalizedString("Traffic:")
    static let unknown = NSLocalizedString("Unknown")
    static let unavailable = NSLocalizedString("Unavailable")
    static let notConnected = NSLocalizedString("Not connected")
}
