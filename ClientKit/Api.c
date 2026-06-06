//
//  Api.c
//  Starskiff
//
//  Feixiao rtw88 client shim, shaped like HeliPort's ClientKit API.
//

#include "Api.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_port.h>
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t *api_mutex = NULL;

enum {
    RTW88_SCAN       = 0,
    RTW88_CONNECT    = 1,
    RTW88_DISCONNECT = 2,
    RTW88_GET_STATE  = 3,
    RTW88_GET_BSS    = 4,
    RTW88_GET_RSSI   = 5,
    RTW88_SET_DEBUG  = 6,
    RTW88_GET_LOG    = 7,
    RTW88_POWER_ON   = 8,
    RTW88_POWER_OFF  = 9,
};

enum {
    RTW88_STATE_IDLE = 0,
    RTW88_STATE_SCANNING,
    RTW88_STATE_AUTHENTICATING,
    RTW88_STATE_ASSOCIATING,
    RTW88_STATE_HANDSHAKING,
    RTW88_STATE_CONNECTED,
    RTW88_STATE_DISCONNECTING,
};

enum {
    WLAN_CIPHER_SUITE_NONE = 0x00000000,
    WLAN_CIPHER_SUITE_CCMP = 0x000FAC04,
};

typedef struct {
    char ssid[33];
    char password[64];
} rtw88_connect_args_t;

typedef struct {
    uint32_t state;
    uint8_t  bssid[6];
    char     ssid[33];
    int32_t  rssi;
    uint32_t channel;
    uint8_t  mac_addr[6];
    uint16_t fw_version;
    uint8_t  fw_sub_version;
    char     chip_name[32];
    uint32_t rx_byte_count;
    uint32_t tx_byte_count;
    uint8_t  scan_offload_supported;
    uint8_t  powered;
} rtw88_state_result_t;

static kern_return_t rtw88_call(io_connect_t con, uint32_t selector,
                                const void *in, size_t in_len,
                                void *out, size_t *out_len)
{
    return IOConnectCallStructMethod(con, selector, in, in_len, out, out_len);
}

static kern_return_t rtw88_get_state_with_connection(io_connect_t con,
                                                     rtw88_state_result_t *state)
{
    size_t out_len = sizeof(*state);
    memset(state, 0, sizeof(*state));
    return rtw88_call(con, RTW88_GET_STATE, NULL, 0, state, &out_len);
}

static kern_return_t rtw88_get_state(rtw88_state_result_t *state)
{
    io_connect_t con;
    if (!open_adapter(&con))
        return KERN_FAILURE;
    kern_return_t ret = rtw88_get_state_with_connection(con, state);
    close_adapter(con);
    return ret;
}

static kern_return_t fill_station_info_from_state(station_info_t *info,
                                                  const rtw88_state_result_t *state)
{
    if (!info || !state)
        return KERN_FAILURE;

    memset(info, 0, sizeof(*info));

    if (state->state != RTW88_STATE_CONNECTED)
        return KERN_FAILURE;

    info->version = IOCTL_VERSION;
    info->op_mode = state->channel > 14 ? ITL80211_MODE_11AC : ITL80211_MODE_11N;
    info->max_mcs = -1;
    info->cur_mcs = -1;
    info->channel = state->channel;
    info->band_width = 20;
    info->rssi = (int16_t)state->rssi;
    info->noise = -95;
    info->rate = 6;
    strlcpy((char *)info->ssid, state->ssid, NWID_LEN);
    memcpy(info->bssid, state->bssid, ETHER_ADDR_LEN);
    return KERN_SUCCESS;
}

static int map_rtw88_state(uint32_t state)
{
    switch (state) {
    case RTW88_STATE_SCANNING:
        return ITL80211_S_SCAN;
    case RTW88_STATE_AUTHENTICATING:
    case RTW88_STATE_HANDSHAKING:
        return ITL80211_S_AUTH;
    case RTW88_STATE_ASSOCIATING:
        return ITL80211_S_ASSOC;
    case RTW88_STATE_CONNECTED:
        return ITL80211_S_RUN;
    case RTW88_STATE_DISCONNECTING:
    case RTW88_STATE_IDLE:
    default:
        return ITL80211_S_INIT;
    }
}

static void copy_cstr(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0)
        return;
    memset(dst, 0, dst_len);
    if (src)
        strlcpy(dst, src, dst_len);
}

static bool service_has_class(io_registry_entry_t service, const char *class_name)
{
    bool match = false;
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("IOClass"),
                                                      kCFAllocatorDefault, 0);
    if (value && CFGetTypeID(value) == CFStringGetTypeID()) {
        char buf[64] = {};
        if (CFStringGetCString((CFStringRef)value, buf, sizeof(buf), kCFStringEncodingUTF8))
            match = strcmp(buf, class_name) == 0;
    }
    if (value)
        CFRelease(value);
    return match;
}

static bool parent_chain_has_rtw88(io_registry_entry_t entry)
{
    io_registry_entry_t cur = entry;

    while (cur) {
        if (service_has_class(cur, "RTW88PCIDevice") ||
            service_has_class(cur, "RTW88USBDevice")) {
            if (cur != entry)
                IOObjectRelease(cur);
            return true;
        }

        io_registry_entry_t parent = MACH_PORT_NULL;
        if (IORegistryEntryGetParentEntry(cur, kIOServicePlane, &parent) != KERN_SUCCESS)
            parent = MACH_PORT_NULL;
        if (cur != entry)
            IOObjectRelease(cur);
        cur = parent;
    }

    return false;
}

static bool find_bsd_name(char *out, size_t out_len)
{
    io_iterator_t iter = MACH_PORT_NULL;
    io_service_t service;
    bool found = false;

    if (!out || out_len == 0)
        return false;
    out[0] = '\0';

    if (IOServiceGetMatchingServices(kIOMasterPortDefault,
                                     IOServiceMatching("IOEthernetInterface"),
                                     &iter) != KERN_SUCCESS) {
        return false;
    }

    while ((service = IOIteratorNext(iter)) != MACH_PORT_NULL) {
        if (parent_chain_has_rtw88(service)) {
            CFTypeRef bsd = IORegistryEntryCreateCFProperty(service, CFSTR("BSD Name"),
                                                           kCFAllocatorDefault, 0);
            if (bsd && CFGetTypeID(bsd) == CFStringGetTypeID()) {
                found = CFStringGetCString((CFStringRef)bsd, out, out_len,
                                           kCFStringEncodingUTF8);
            }
            if (bsd)
                CFRelease(bsd);
        }
        IOObjectRelease(service);
        if (found)
            break;
    }

    IOObjectRelease(iter);
    return found;
}

static bool open_service_named(const char *service_name, io_connect_t *connection_t)
{
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                       IOServiceMatching(service_name));
    if (!service)
        return false;

    kern_return_t ret = IOServiceOpen(service, mach_task_self(), 0, connection_t);
    IOObjectRelease(service);
    return ret == KERN_SUCCESS;
}

bool open_adapter(io_connect_t *connection_t)
{
    if (!connection_t)
        return false;

    bool found = open_service_named("RTW88PCIDevice", connection_t) ||
                 open_service_named("RTW88USBDevice", connection_t);

    if (found) {
        if (!api_mutex) {
            api_mutex = malloc(sizeof(pthread_mutex_t));
            pthread_mutex_init(api_mutex, NULL);
        }
        pthread_mutex_lock(api_mutex);
    }

    return found;
}

void close_adapter(io_connect_t connection)
{
    if (connection) {
        IOServiceClose(connection);
        pthread_mutex_unlock(api_mutex);
    }
}

bool get_platform_info(platform_info_t *info)
{
    if (!info)
        return false;

    memset(info, 0, sizeof(*info));

    rtw88_state_result_t state;
    if (rtw88_get_state(&state) != KERN_SUCCESS)
        return false;

    char bsd[32] = {};
    if (!find_bsd_name(bsd, sizeof(bsd)))
        strlcpy(bsd, "rtw88", sizeof(bsd));

    copy_cstr(info->device_info_str, sizeof(info->device_info_str), bsd);
    snprintf(info->driver_info_str, sizeof(info->driver_info_str),
             "%s fw %u.%u",
             state.chip_name[0] ? state.chip_name : "rtw88",
             state.fw_version, state.fw_sub_version);
    return true;
}

bool get_hardware_info(hardware_info_t *info)
{
    if (!info)
        return false;

    memset(info, 0, sizeof(*info));

    io_connect_t con;
    if (!open_adapter(&con))
        return false;

    rtw88_state_result_t state;
    kern_return_t ret = rtw88_get_state_with_connection(con, &state);
    close_adapter(con);
    if (ret != KERN_SUCCESS)
        return false;

    char bsd[32] = {};
    if (!find_bsd_name(bsd, sizeof(bsd)))
        strlcpy(bsd, "rtw88", sizeof(bsd));

    strlcpy(info->interface_name, bsd, sizeof(info->interface_name));
    strlcpy(info->chip_name,
            state.chip_name[0] ? state.chip_name : "rtw88",
            sizeof(info->chip_name));
    strlcpy(info->ssid, state.ssid, sizeof(info->ssid));
    memcpy(info->mac_addr, state.mac_addr, sizeof(info->mac_addr));
    memcpy(info->bssid, state.bssid, sizeof(info->bssid));
    info->fw_version = state.fw_version;
    info->fw_sub_version = state.fw_sub_version;
    info->state = state.state;
    info->rssi = state.rssi;
    info->channel = state.channel;
    info->rx_byte_count = state.rx_byte_count;
    info->tx_byte_count = state.tx_byte_count;
    info->power_on = state.powered ? 1 : 0;
    info->scan_offload_supported = state.scan_offload_supported;
    return true;
}

bool get_power_state(bool *enabled)
{
    if (!enabled)
        return false;

    rtw88_state_result_t state;
    if (rtw88_get_state(&state) != KERN_SUCCESS)
        return false;

    *enabled = state.powered != 0;
    return true;
}

bool get_80211_state(uint32_t *state)
{
    if (!state)
        return false;

    rtw88_state_result_t rtw_state;
    if (rtw88_get_state(&rtw_state) != KERN_SUCCESS)
        return false;

    *state = (uint32_t)map_rtw88_state(rtw_state.state);
    return true;
}

bool get_network_ssid(char *ssid)
{
    if (!ssid)
        return false;

    rtw88_state_result_t state;
    if (rtw88_get_state(&state) != KERN_SUCCESS)
        return false;

    memset(ssid, 0, NWID_LEN);
    strlcpy(ssid, state.ssid, NWID_LEN);
    return true;
}

bool get_network_bssid(char *bssid)
{
    if (!bssid)
        return false;

    rtw88_state_result_t state;
    if (rtw88_get_state(&state) != KERN_SUCCESS)
        return false;

    memcpy(bssid, state.bssid, ETHER_ADDR_LEN);
    return true;
}

static void fill_security(struct ioctl_network_info *info, uint32_t cipher)
{
    if (cipher == WLAN_CIPHER_SUITE_CCMP) {
        info->supported_rsnprotos = ITL80211_PROTO_RSN;
        info->rsn_protos = ITL80211_WPA_PROTO_WPA2;
        info->supported_rsnakms = ITL80211_AKM_PSK;
        info->rsn_akms = ITL80211_AKM_PSK;
        info->rsn_ciphers = ITL80211_CIPHER_CCMP;
        info->rsn_groupcipher = ITL80211_CIPHER_CCMP;
        info->ni_rsncipher = ITL80211_CIPHER_CCMP;
    } else {
        info->supported_rsnprotos = ITL80211_PROTO_NONE;
        info->rsn_protos = 0;
        info->supported_rsnakms = ITL80211_AKM_NONE;
        info->rsn_akms = ITL80211_AKM_NONE;
        info->rsn_ciphers = ITL80211_CIPHER_NONE;
        info->rsn_groupcipher = ITL80211_CIPHER_NONE;
        info->ni_rsncipher = ITL80211_CIPHER_NONE;
    }
}

static bool read_network_list_with_connection(io_connect_t con,
                                              network_info_list_t *list)
{
    if (!list)
        return false;

    memset(list, 0, sizeof(*list));

    uint8_t raw[4096] = {};
    size_t raw_len = sizeof(raw);
    kern_return_t ret = rtw88_call(con, RTW88_GET_BSS, NULL, 0, raw, &raw_len);

    if (ret != KERN_SUCCESS || raw_len < sizeof(uint32_t))
        return false;

    uint32_t total = 0;
    memcpy(&total, raw, sizeof(total));
    if (total > raw_len)
        total = (uint32_t)raw_len;

    uint32_t off = sizeof(uint32_t);
    while (off + 1 <= total && list->count < MAX_NETWORK_LIST_LENGTH) {
        uint8_t ssid_len = raw[off++];
        if (ssid_len > NWID_LEN || off + ssid_len + 6 + 2 + 1 + 4 > total)
            break;

        struct ioctl_network_info *info = &list->networks[list->count];
        memset(info, 0, sizeof(*info));

        memcpy(info->ssid, raw + off, ssid_len);
        off += ssid_len;
        memcpy(info->bssid, raw + off, ETHER_ADDR_LEN);
        off += ETHER_ADDR_LEN;

        int16_t rssi = (int16_t)((raw[off] << 8) | raw[off + 1]);
        off += 2;
        info->rssi = rssi;
        info->noise = -95;
        info->channel = raw[off++];

        uint32_t cipher = 0;
        memcpy(&cipher, raw + off, sizeof(cipher));
        off += sizeof(cipher);
        fill_security(info, cipher);

        list->count++;
    }

    return true;
}

bool get_cached_network_list(network_info_list_t *list)
{
    io_connect_t con;
    if (!open_adapter(&con))
        return false;

    bool ok = read_network_list_with_connection(con, list);
    close_adapter(con);
    return ok;
}

bool get_network_list(network_info_list_t *list)
{
    if (!list)
        return false;

    memset(list, 0, sizeof(*list));

    io_connect_t con;
    if (!open_adapter(&con))
        return false;

    rtw88_state_result_t state;
    bool can_scan = true;
    bool wait_for_active_scan = false;
    if (rtw88_get_state_with_connection(con, &state) == KERN_SUCCESS) {
        bool connecting = state.state == RTW88_STATE_AUTHENTICATING ||
                          state.state == RTW88_STATE_ASSOCIATING ||
                          state.state == RTW88_STATE_HANDSHAKING;
        bool connected_no_offload = state.state == RTW88_STATE_CONNECTED &&
                                    !state.scan_offload_supported;
        wait_for_active_scan = state.state == RTW88_STATE_SCANNING;
        can_scan = !wait_for_active_scan && !connecting && !connected_no_offload;
    }

    if (can_scan) {
        (void)rtw88_call(con, RTW88_SCAN, NULL, 0, NULL, NULL);
        wait_for_active_scan = true;
    }

    if (wait_for_active_scan) {
        for (int i = 0; i < 120; i++) {
            if (rtw88_get_state_with_connection(con, &state) == KERN_SUCCESS &&
                state.state != RTW88_STATE_SCANNING) {
                break;
            }
            usleep(100000);
        }
    }

    bool ok = read_network_list_with_connection(con, list);
    close_adapter(con);
    return ok;
}

bool connect_network(const char *ssid, const char *pwd)
{
    if (associate_ssid(ssid, pwd) != KERN_SUCCESS)
        return false;

    for (int timeout = 0; timeout < 30; timeout++) {
        sleep(1);

        uint32_t state = 0;
        if (get_80211_state(&state) && state == ITL80211_S_RUN) {
            station_info_t sta_info;
            if (get_station_info(&sta_info) == KERN_SUCCESS)
                return strncmp(ssid, (char *)sta_info.ssid, NWID_LEN) == 0;
        }
    }

    return false;
}

bool is_power_on(void)
{
    bool enabled = false;
    return get_power_state(&enabled) && enabled;
}

kern_return_t get_station_info(station_info_t *info)
{
    if (!info)
        return KERN_FAILURE;

    memset(info, 0, sizeof(*info));

    rtw88_state_result_t state;
    kern_return_t ret = rtw88_get_state(&state);
    if (ret != KERN_SUCCESS)
        return ret;

    return fill_station_info_from_state(info, &state);
}

kern_return_t power_on(void)
{
    io_connect_t con;
    if (!open_adapter(&con))
        return KERN_FAILURE;
    kern_return_t ret = rtw88_call(con, RTW88_POWER_ON, NULL, 0, NULL, NULL);
    close_adapter(con);
    return ret;
}

kern_return_t power_off(void)
{
    io_connect_t con;
    if (!open_adapter(&con))
        return KERN_FAILURE;
    kern_return_t ret = rtw88_call(con, RTW88_POWER_OFF, NULL, 0, NULL, NULL);
    close_adapter(con);
    return ret;
}

kern_return_t join_ssid(const char *ssid, const char *pwd)
{
    return associate_ssid(ssid, pwd);
}

kern_return_t associate_ssid(const char *ssid, const char *pwd)
{
    if (!ssid)
        return KERN_FAILURE;

    io_connect_t con;
    if (!open_adapter(&con))
        return KERN_FAILURE;

    rtw88_connect_args_t args;
    memset(&args, 0, sizeof(args));
    strlcpy(args.ssid, ssid, sizeof(args.ssid));
    if (pwd)
        strlcpy(args.password, pwd, sizeof(args.password));

    kern_return_t ret = rtw88_call(con, RTW88_CONNECT, &args, sizeof(args), NULL, NULL);
    close_adapter(con);
    return ret;
}

kern_return_t dis_associate_ssid(const char *ssid)
{
    (void)ssid;
    return ioctl_set(IOCTL_80211_DISASSOCIATE, NULL, 0);
}

kern_return_t _nake_ioctl(io_connect_t con, int *ctl, bool is_get, void *data, size_t data_len)
{
    if (!ctl)
        return KERN_FAILURE;

    if (!is_get) {
        switch (*ctl & ~IOCTL_MASK) {
        case IOCTL_80211_ASSOCIATE:
        case IOCTL_80211_JOIN: {
            const struct ioctl_associate *ass = (const struct ioctl_associate *)data;
            if (!ass)
                return KERN_FAILURE;
            rtw88_connect_args_t args;
            memset(&args, 0, sizeof(args));
            strlcpy(args.ssid, ass->nwid.nwid, sizeof(args.ssid));
            strlcpy(args.password, ass->wpa_key.key, sizeof(args.password));
            return rtw88_call(con, RTW88_CONNECT, &args, sizeof(args), NULL, NULL);
        }
        case IOCTL_80211_DISASSOCIATE:
            return rtw88_call(con, RTW88_DISCONNECT, NULL, 0, NULL, NULL);
        case IOCTL_80211_SCAN:
            return rtw88_call(con, RTW88_SCAN, NULL, 0, NULL, NULL);
        case IOCTL_80211_POWER: {
            const struct ioctl_power *power = (const struct ioctl_power *)data;
            if (power && !power->enabled)
                return rtw88_call(con, RTW88_POWER_OFF, NULL, 0, NULL, NULL);
            return rtw88_call(con, RTW88_POWER_ON, NULL, 0, NULL, NULL);
        }
        default:
            return KERN_FAILURE;
        }
    }

    rtw88_state_result_t state;
    switch (*ctl) {
    case IOCTL_80211_DRIVER_INFO: {
        if (rtw88_get_state_with_connection(con, &state) != KERN_SUCCESS)
            return KERN_FAILURE;
        char bsd[32] = {};
        if (!find_bsd_name(bsd, sizeof(bsd)))
            strlcpy(bsd, "rtw88", sizeof(bsd));
        struct ioctl_driver_info *info = (struct ioctl_driver_info *)data;
        memset(info, 0, data_len);
        info->version = IOCTL_VERSION;
        strlcpy(info->bsd_name, bsd, sizeof(info->bsd_name));
        strlcpy(info->driver_version, "Starskiff", sizeof(info->driver_version));
        snprintf(info->fw_version, sizeof(info->fw_version),
                 "%s fw %u.%u",
                 state.chip_name[0] ? state.chip_name : "rtw88",
                 state.fw_version, state.fw_sub_version);
        return KERN_SUCCESS;
    }
    case IOCTL_80211_STA_INFO:
        if (rtw88_get_state_with_connection(con, &state) != KERN_SUCCESS)
            return KERN_FAILURE;
        return fill_station_info_from_state((station_info_t *)data, &state);
    case IOCTL_80211_POWER: {
        struct ioctl_power *power = (struct ioctl_power *)data;
        memset(power, 0, sizeof(*power));
        power->version = IOCTL_VERSION;
        if (rtw88_get_state_with_connection(con, &state) != KERN_SUCCESS)
            return KERN_FAILURE;
        power->enabled = state.powered ? 1 : 0;
        return KERN_SUCCESS;
    }
    case IOCTL_80211_STATE: {
        if (rtw88_get_state_with_connection(con, &state) != KERN_SUCCESS)
            return KERN_FAILURE;
        struct ioctl_state *out = (struct ioctl_state *)data;
        memset(out, 0, sizeof(*out));
        out->version = IOCTL_VERSION;
        out->state = map_rtw88_state(state.state);
        return KERN_SUCCESS;
    }
    case IOCTL_80211_NW_ID: {
        if (rtw88_get_state_with_connection(con, &state) != KERN_SUCCESS)
            return KERN_FAILURE;
        struct ioctl_nw_id *out = (struct ioctl_nw_id *)data;
        memset(out, 0, sizeof(*out));
        out->version = IOCTL_VERSION;
        out->len = (unsigned int)strnlen(state.ssid, sizeof(state.ssid));
        memcpy(out->nwid, state.ssid, out->len > NWID_LEN ? NWID_LEN : out->len);
        return KERN_SUCCESS;
    }
    case IOCTL_80211_NW_BSSID: {
        if (rtw88_get_state_with_connection(con, &state) != KERN_SUCCESS)
            return KERN_FAILURE;
        struct ioctl_nw_bssid *out = (struct ioctl_nw_bssid *)data;
        memset(out, 0, sizeof(*out));
        out->version = IOCTL_VERSION;
        memcpy(out->bssid, state.bssid, ETHER_ADDR_LEN);
        return KERN_SUCCESS;
    }
    default:
        return KERN_FAILURE;
    }
}

kern_return_t _ioctl(int ctl, bool is_get, void *data, size_t data_len)
{
    io_connect_t con;
    if (!open_adapter(&con))
        return KERN_FAILURE;

    kern_return_t ret = _nake_ioctl(con, &ctl, is_get, data, data_len);
    close_adapter(con);
    return ret;
}

kern_return_t ioctl_set(int ctl, void *data, size_t data_len)
{
    return _ioctl(ctl, false, data, data_len);
}

kern_return_t ioctl_get(int ctl, void *data, size_t data_len)
{
    return _ioctl(ctl, true, data, data_len);
}

void api_terminate(void)
{
    if (api_mutex) {
        pthread_mutex_lock(api_mutex);
        pthread_mutex_unlock(api_mutex);
        pthread_mutex_destroy(api_mutex);
        free(api_mutex);
        api_mutex = NULL;
    }
}
