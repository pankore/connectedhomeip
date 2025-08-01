/*
 *
 *    Copyright (c) 2024-2025 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Provides the wrapper for Zephyr WiFi API
 */

#include "WiFiManager.h"

#include <crypto/RandUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/Zephyr/InetUtils.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_stats.h>
#include <zephyr/version.h>

extern "C" {
#include <common/defs.h>
#include <wpa_supplicant/config.h>
#include <wpa_supplicant/driver_i.h>
#include <wpa_supplicant/scan.h>

// extern function to obtain bssid from status buffer
// It is defined in zephyr/subsys/net/ip/utils.c
extern char * net_sprint_ll_addr_buf(const uint8_t * ll, uint8_t ll_len, char * buf, int buflen);
}

namespace chip {
namespace DeviceLayer {

namespace {

app::Clusters::NetworkCommissioning::WiFiBandEnum ConvertBandEnum(uint8_t band)
{
    switch (band)
    {
    case WIFI_FREQ_BAND_2_4_GHZ:
        return app::Clusters::NetworkCommissioning::WiFiBandEnum::k2g4;
    case WIFI_FREQ_BAND_5_GHZ:
        return app::Clusters::NetworkCommissioning::WiFiBandEnum::k5g;
    case WIFI_FREQ_BAND_6_GHZ:
        return app::Clusters::NetworkCommissioning::WiFiBandEnum::k6g;
    default:
        return app::Clusters::NetworkCommissioning::WiFiBandEnum::kUnknownEnumValue;
    }
}

NetworkCommissioning::WiFiScanResponse ToScanResponse(const wifi_scan_result * result)
{
    NetworkCommissioning::WiFiScanResponse response = {};

    if (result != nullptr)
    {
        static_assert(sizeof(response.bssid) == sizeof(result->mac), "BSSID length mismatch");
        assert(sizeof(response.ssid) >= result->ssid_length);
        // TODO: Distinguish WPA versions
        response.security.Set(result->security == WIFI_SECURITY_TYPE_PSK ? NetworkCommissioning::WiFiSecurity::kWpaPersonal
                                                                         : NetworkCommissioning::WiFiSecurity::kUnencrypted);
        response.channel  = result->channel;
        response.rssi     = result->rssi;
        response.ssidLen  = result->ssid_length;
        response.wiFiBand = ConvertBandEnum(result->band);
        memcpy(response.ssid, result->ssid, result->ssid_length);
        // TODO: MAC/BSSID is not filled by the Wi-Fi driver
        memcpy(response.bssid, result->mac, result->mac_length);
    }

    return response;
}

// Matter expectations towards Wi-Fi version codes are unaligned with
// what wpa_supplicant provides. This function maps supplicant codes
// to the ones defined in the Matter spec (11.14.5.2. WiFiVersionEnum)
app::Clusters::WiFiNetworkDiagnostics::WiFiVersionEnum MapToMatterWiFiVersionCode(wifi_link_mode wifiVersion)
{
    using app::Clusters::WiFiNetworkDiagnostics::WiFiVersionEnum;

    if (wifiVersion < WIFI_1 || wifiVersion > WIFI_6E)
    {
        ChipLogError(DeviceLayer, "Unsupported Wi-Fi version detected");
        return WiFiVersionEnum::kA; // let's return 'a' by default
    }

    switch (wifiVersion)
    {
    case WIFI_1:
        return WiFiVersionEnum::kB;
    case WIFI_2:
        return WiFiVersionEnum::kA;
    case WIFI_6E:
        return WiFiVersionEnum::kAx; // treat as 802.11ax
    default:
        break;
    }

    return static_cast<WiFiVersionEnum>(wifiVersion - 1);
}

// Matter expectations towards Wi-Fi security type codes are unaligned with
// what wpa_supplicant provides. This function maps supplicant codes
// to the ones defined in the Matter spec (11.14.3.1. SecurityType enum)
app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum MapToMatterSecurityType(wifi_security_type securityType)
{
    using app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum;

    switch (securityType)
    {
    case WIFI_SECURITY_TYPE_NONE:
        return SecurityTypeEnum::kNone;
    case WIFI_SECURITY_TYPE_PSK:
    case WIFI_SECURITY_TYPE_PSK_SHA256:
        return SecurityTypeEnum::kWpa2;
    case WIFI_SECURITY_TYPE_SAE:
        return SecurityTypeEnum::kWpa3;
    default:
        break;
    }

    return SecurityTypeEnum::kUnspecified;
}

} // namespace

const Map<wifi_iface_state, WiFiManager::StationStatus, 10>
    WiFiManager::sStatusMap({ { WIFI_STATE_DISCONNECTED, WiFiManager::StationStatus::DISCONNECTED },
                              { WIFI_STATE_INTERFACE_DISABLED, WiFiManager::StationStatus::DISABLED },
                              { WIFI_STATE_INACTIVE, WiFiManager::StationStatus::DISABLED },
                              { WIFI_STATE_SCANNING, WiFiManager::StationStatus::SCANNING },
                              { WIFI_STATE_AUTHENTICATING, WiFiManager::StationStatus::CONNECTING },
                              { WIFI_STATE_ASSOCIATING, WiFiManager::StationStatus::CONNECTING },
                              { WIFI_STATE_ASSOCIATED, WiFiManager::StationStatus::CONNECTED },
                              { WIFI_STATE_4WAY_HANDSHAKE, WiFiManager::StationStatus::PROVISIONING },
                              { WIFI_STATE_GROUP_HANDSHAKE, WiFiManager::StationStatus::PROVISIONING },
                              { WIFI_STATE_COMPLETED, WiFiManager::StationStatus::FULLY_PROVISIONED } });

const Map<uint32_t, WiFiManager::NetEventHandler, 5> WiFiManager::sEventHandlerMap({
    { NET_EVENT_WIFI_SCAN_RESULT, WiFiManager::ScanResultHandler },
    { NET_EVENT_WIFI_SCAN_DONE, WiFiManager::ScanDoneHandler },
    { NET_EVENT_WIFI_CONNECT_RESULT, WiFiManager::ConnectHandler },
    { NET_EVENT_WIFI_DISCONNECT_RESULT, WiFiManager::DisconnectHandler },
    { NET_EVENT_WIFI_DISCONNECT_COMPLETE, WiFiManager::DisconnectHandler },
});

void WiFiManager::WifiMgmtEventHandler(net_mgmt_event_callback * cb, uint32_t mgmtEvent, net_if * iface)
{
    if (iface == Instance().mNetIf)
    {
        Platform::UniquePtr<uint8_t> eventData(new uint8_t[cb->info_length]);
        VerifyOrReturn(eventData);
        memcpy(eventData.get(), cb->info, cb->info_length);
        sEventHandlerMap[mgmtEvent](std::move(eventData), cb->info_length);
    }
}

void WiFiManager::IPv6MgmtEventHandler(net_mgmt_event_callback * cb, uint32_t mgmtEvent, net_if * iface)
{
    if (((mgmtEvent == NET_EVENT_IPV6_ADDR_ADD) || (mgmtEvent == NET_EVENT_IPV6_ADDR_DEL)) && cb->info)
    {
        IPv6AddressChangeHandler(cb->info);
    }
}

CHIP_ERROR WiFiManager::Init()
{
    mNetIf = InetUtils::GetWiFiInterface();
    VerifyOrReturnError(mNetIf != nullptr, INET_ERROR_UNKNOWN_INTERFACE);

    net_mgmt_init_event_callback(&mWiFiMgmtClbk, WifiMgmtEventHandler, kWifiManagementEvents);
    net_mgmt_init_event_callback(&mIPv6MgmtClbk, IPv6MgmtEventHandler, kIPv6ManagementEvents);

    net_mgmt_add_event_callback(&mWiFiMgmtClbk);
    net_mgmt_add_event_callback(&mIPv6MgmtClbk);

    ChipLogDetail(DeviceLayer, "WiFiManager has been initialized");

    return CHIP_NO_ERROR;
}
CHIP_ERROR WiFiManager::Scan(const ByteSpan & ssid, ScanResultCallback resultCallback, ScanDoneCallback doneCallback,
                             bool internalScan)
{
    mInternalScan       = internalScan;
    mScanResultCallback = resultCallback;
    mScanDoneCallback   = doneCallback;
    mCachedWiFiState    = mWiFiState;
    mWiFiState          = WIFI_STATE_SCANNING;
    mSsidFound          = false;

    wifi_scan_params * scanParams{ nullptr };
    size_t scanParamsSize{ 0 };

    if (!ssid.empty())
    {
        /* We must assume that the ssid is handled as a NULL-terminated string.
           Note that the mScanSsidBuffer is initialized with zeros. */
        VerifyOrReturnError(ssid.size() < sizeof(mScanSsidBuffer), CHIP_ERROR_INVALID_ARGUMENT);
        memcpy(mScanSsidBuffer, ssid.data(), ssid.size());
        mScanSsidBuffer[ssid.size()] = 0; // indicate the end of ssid string
        mScanParams.ssids[0]         = mScanSsidBuffer;
#if (CONFIG_WIFI_MGMT_SCAN_SSID_FILT_MAX > 1)
        mScanParams.ssids[1] = nullptr; // indicate the end of ssids list
#endif
        scanParams     = &mScanParams;
        scanParamsSize = sizeof(*scanParams);
    }
    if (0 != net_mgmt(NET_REQUEST_WIFI_SCAN, mNetIf, scanParams, scanParamsSize))
    {
        ChipLogError(DeviceLayer, "Scan request failed");
        return CHIP_ERROR_INTERNAL;
    }

    ChipLogDetail(DeviceLayer, "WiFi scanning started...");

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::ClearStationProvisioningData()
{
    mWiFiParams.mRssi = std::numeric_limits<int8_t>::min();
    memset(&mWiFiParams.mParams, 0, sizeof(mWiFiParams.mParams));
    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::Connect(const ByteSpan & ssid, const ByteSpan & credentials, const ConnectionHandling & handling)
{
    ChipLogDetail(DeviceLayer, "Connecting to WiFi network: %s", NullTerminated(ssid).c_str());

    mHandling = handling;

    mWiFiState = WIFI_STATE_ASSOCIATING;

    // Store SSID and credentials and perform the scan to detect the security mode supported by the AP.
    // Zephyr WiFi connect request will be issued in the callback when we have the SSID match.
    mWantedNetwork.Erase();
    memcpy(mWantedNetwork.ssid, ssid.data(), ssid.size());
    memcpy(mWantedNetwork.pass, credentials.data(), credentials.size());
    mWantedNetwork.ssidLen = ssid.size();
    mWantedNetwork.passLen = credentials.size();

    return Scan(ssid, nullptr, nullptr, true /* internal scan */);
}

CHIP_ERROR WiFiManager::Disconnect()
{
    mApplicationDisconnectRequested = true;
    int status                      = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, mNetIf, NULL, 0);

    if (status)
    {
        mApplicationDisconnectRequested = false;
        if (status == -EALREADY)
        {
            ChipLogDetail(DeviceLayer, "Already disconnected");
        }
        else
        {
            ChipLogDetail(DeviceLayer, "Disconnect request failed");
            return CHIP_ERROR_INTERNAL;
        }
    }
    else
    {
        ChipLogDetail(DeviceLayer, "Disconnect requested");
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::GetWiFiInfo(WiFiInfo & info) const
{
    wifi_iface_status status = { 0 };

    if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, mNetIf, &status, sizeof(wifi_iface_status)))
    {
        ChipLogError(DeviceLayer, "Status request failed");
        return CHIP_ERROR_INTERNAL;
    }

    if (status.state >= WIFI_STATE_ASSOCIATED)
    {
        info.mSecurityType = MapToMatterSecurityType(status.security);
        info.mWiFiVersion  = MapToMatterWiFiVersionCode(status.link_mode);
        info.mRssi         = static_cast<int8_t>(status.rssi);
        info.mChannel      = static_cast<uint16_t>(status.channel);
        info.mSsidLen      = status.ssid_len;
#if KERNEL_VERSION_MAJOR >= 4 && KERNEL_VERSION_MINOR >= 1
        info.mCurrentPhyRate = static_cast<uint64_t>(status.current_phy_tx_rate);
#else
        info.mCurrentPhyRate = static_cast<uint64_t>(status.current_phy_rate);
#endif
        memcpy(info.mSsid, status.ssid, status.ssid_len);
        memcpy(info.mBssId, status.bssid, sizeof(status.bssid));

        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_INTERNAL;
}

CHIP_ERROR WiFiManager::GetNetworkStatistics(NetworkStatistics & stats) const
{
    net_stats_wifi data{};
    net_mgmt(NET_REQUEST_STATS_GET_WIFI, mNetIf, &data, sizeof(data));

    stats.mPacketMulticastRxCount = data.multicast.rx;
    stats.mPacketMulticastTxCount = data.multicast.tx;
    stats.mPacketUnicastRxCount   = data.unicast.rx;
    stats.mPacketUnicastTxCount   = data.unicast.tx;
    stats.mOverRunCount           = data.overrun_count;
    stats.mBeaconsSuccessCount    = data.sta_mgmt.beacons_rx;
    stats.mBeaconsLostCount       = data.sta_mgmt.beacons_miss;

    return CHIP_NO_ERROR;
}

void WiFiManager::ScanResultHandler(Platform::UniquePtr<uint8_t> data, size_t length)
{
    // Validate that input data size matches the expected one.
    VerifyOrReturn(length == sizeof(wifi_scan_result));

    // Contrary to other handlers, offload accumulating of the scan results from the CHIP thread to the caller's thread
    const wifi_scan_result * scanResult = reinterpret_cast<const wifi_scan_result *>(data.get());

    if (Instance().mInternalScan &&
        Instance().mWantedNetwork.GetSsidSpan().data_equal(ByteSpan(scanResult->ssid, scanResult->ssid_length)))
    {
        // Prepare the connection parameters
        // In case there are many networks with the same SSID choose the one with the best RSSI
        if (scanResult->rssi > Instance().mWiFiParams.mRssi)
        {
            Instance().ClearStationProvisioningData();
            Instance().mWiFiParams.mParams.ssid_length = static_cast<uint8_t>(Instance().mWantedNetwork.ssidLen);
            Instance().mWiFiParams.mParams.ssid        = Instance().mWantedNetwork.ssid;
            // Fallback to the WIFI_SECURITY_TYPE_PSK if the security is unknown
            Instance().mWiFiParams.mParams.security =
                scanResult->security <= WIFI_SECURITY_TYPE_MAX ? scanResult->security : WIFI_SECURITY_TYPE_PSK;
            Instance().mWiFiParams.mParams.psk_length = static_cast<uint8_t>(Instance().mWantedNetwork.passLen);
            Instance().mWiFiParams.mParams.mfp        = scanResult->mfp;
            Instance().mWiFiParams.mParams.band       = scanResult->band;

            // If the security is none, WiFi driver expects the psk to be nullptr
            if (Instance().mWiFiParams.mParams.security == WIFI_SECURITY_TYPE_NONE)
            {
                Instance().mWiFiParams.mParams.psk = nullptr;
            }
            else
            {
                Instance().mWiFiParams.mParams.psk = Instance().mWantedNetwork.pass;
            }

            Instance().mWiFiParams.mParams.timeout = Instance().mHandling.mConnectionTimeout.count();
            Instance().mWiFiParams.mParams.channel = WIFI_CHANNEL_ANY;
            Instance().mWiFiParams.mRssi           = scanResult->rssi;
            Instance().mSsidFound                  = true;
        }
    }

    if (Instance().mScanResultCallback && !Instance().mInternalScan)
    {
        Instance().mScanResultCallback(ToScanResponse(scanResult));
    }
}

void WiFiManager::ScanDoneHandler(Platform::UniquePtr<uint8_t> data, size_t length)
{
    // Validate that input data size matches the expected one.
    VerifyOrReturn(length == sizeof(wifi_status));

    CHIP_ERROR err = SystemLayer().ScheduleLambda([capturedData = data.get()] {
        Platform::UniquePtr<uint8_t> safePtr(capturedData);
        uint8_t * rawData             = safePtr.get();
        const wifi_status * status    = reinterpret_cast<const wifi_status *>(rawData);
        ScanDoneStatus scanDoneStatus = status->status;

        if (scanDoneStatus)
        {
            ChipLogError(DeviceLayer, "Wi-Fi scan finalization failure (%d)", scanDoneStatus);
        }
        else
        {
            ChipLogProgress(DeviceLayer, "Wi-Fi scan done");
        }

        if (Instance().mScanDoneCallback && !Instance().mInternalScan)
        {
            Instance().mScanDoneCallback(scanDoneStatus);
            // restore the connection state from before the scan request was issued
            Instance().mWiFiState = Instance().mCachedWiFiState;
            return;
        }

        // Internal scan is supposed to be followed by a connection request if the SSID has been found
        if (Instance().mInternalScan)
        {

            if (!Instance().mSsidFound)
            {
                ChipLogProgress(DeviceLayer, "No requested SSID found");
                auto currentTimeout = Instance().CalculateNextRecoveryTime();
                ChipLogProgress(DeviceLayer, "Starting connection recover: re-scanning... (next attempt in %d ms)",
                                currentTimeout.count());
                DeviceLayer::SystemLayer().StartTimer(currentTimeout, Recover, nullptr);
                return;
            }

            Instance().mWiFiState = WIFI_STATE_ASSOCIATING;
            net_if * iface        = InetUtils::GetInterface();
            VerifyOrReturn(nullptr != iface, CHIP_ERROR_INTERNAL);

            if (net_mgmt(NET_REQUEST_WIFI_CONNECT, Instance().mNetIf, &(Instance().mWiFiParams.mParams),
                         sizeof(wifi_connect_req_params)))
            {
                ChipLogError(DeviceLayer, "Connection request failed");
                if (Instance().mHandling.mOnConnectionDone)
                {
                    Instance().mHandling.mOnConnectionDone(WIFI_STATUS_CONN_FAIL);
                }
                Instance().mWiFiState = WIFI_STATE_DISCONNECTED;
                return;
            }
            ChipLogProgress(DeviceLayer, "Connection to %*s requested [RSSI=%d]", Instance().mWiFiParams.mParams.ssid_length,
                            Instance().mWiFiParams.mParams.ssid, Instance().mWiFiParams.mRssi);
            Instance().mInternalScan = false;
        }
    });

    if (CHIP_NO_ERROR == err)
    {
        // the ownership has been transferred to the worker thread - release the buffer
        data.release();
    }
}

void WiFiManager::SendRouterSolicitation(System::Layer * layer, void * param)
{
    net_if_start_rs(Instance().mNetIf);
    Instance().mRouterSolicitationCounter++;
    if (Instance().mRouterSolicitationCounter < kRouterSolicitationMaxCount)
    {
        DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(kRouterSolicitationIntervalMs), SendRouterSolicitation,
                                              nullptr);
    }
    else
    {
        Instance().mRouterSolicitationCounter = 0;
    }
}

void WiFiManager::ConnectHandler(Platform::UniquePtr<uint8_t> data, size_t length)
{
    using app::Clusters::WiFiNetworkDiagnostics::AssociationFailureCauseEnum;

    // Validate that input data size matches the expected one.
    VerifyOrReturn(length == sizeof(wifi_status));

    CHIP_ERROR err = SystemLayer().ScheduleLambda([capturedData = data.get()] {
        Platform::UniquePtr<uint8_t> safePtr(capturedData);
        uint8_t * rawData           = safePtr.get();
        const wifi_status * status  = reinterpret_cast<const wifi_status *>(rawData);
        wifi_conn_status connStatus = status->conn_status;

        if (connStatus)
        {
            ChipLogProgress(DeviceLayer, "Connection to WiFi network failed or was terminated by another request");
            Instance().mWiFiState = WIFI_STATE_DISCONNECTED;
            if (Instance().mHandling.mOnConnectionDone)
            {
                Instance().mHandling.mOnConnectionDone(connStatus);
            }

            WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();
            if (delegate)
            {
                uint16_t reason = Instance().GetLastDisconnectReason();
                uint8_t associationFailureCause;

                switch (connStatus)
                {
                case WIFI_STATUS_CONN_WRONG_PASSWORD:
                    associationFailureCause = to_underlying(AssociationFailureCauseEnum::kAuthenticationFailed);
                    break;
                case WIFI_STATUS_CONN_FAIL:
                case WIFI_STATUS_CONN_TIMEOUT:
                    associationFailureCause = to_underlying(AssociationFailureCauseEnum::kAssociationFailed);
                    break;
                case WIFI_STATUS_CONN_AP_NOT_FOUND:
                    associationFailureCause = to_underlying(AssociationFailureCauseEnum::kSsidNotFound);
                    break;
                default:
                    associationFailureCause = to_underlying(AssociationFailureCauseEnum::kUnknown);
                    break;
                }

                delegate->OnAssociationFailureDetected(associationFailureCause, reason);
            }
        }
        else // The connection has been established successfully.
        {
            // Workaround needed until sending Router Solicitation after connect will be done by the driver.
            DeviceLayer::SystemLayer().StartTimer(
                System::Clock::Milliseconds32(chip::Crypto::GetRandU16() % kMaxInitialRouterSolicitationDelayMs),
                SendRouterSolicitation, nullptr);

            ChipLogProgress(DeviceLayer, "Connected to WiFi network");
            Instance().mWiFiState = WIFI_STATE_COMPLETED;
            if (Instance().mHandling.mOnConnectionDone)
            {
                Instance().mHandling.mOnConnectionDone(connStatus);
            }
            Instance().PostConnectivityStatusChange(kConnectivity_Established);

            // Workaround needed to re-initialize mDNS server after Wi-Fi interface is operative
            chip::DeviceLayer::ChipDeviceEvent event;
            event.Type = chip::DeviceLayer::DeviceEventType::kDnssdInitialized;

            CHIP_ERROR error = chip::DeviceLayer::PlatformMgr().PostEvent(&event);
            if (error != CHIP_NO_ERROR)
            {
                ChipLogError(DeviceLayer, "Cannot post event: %" CHIP_ERROR_FORMAT, error.Format());
            }

            WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();
            if (delegate)
            {
                delegate->OnConnectionStatusChanged(
                    to_underlying(app::Clusters::WiFiNetworkDiagnostics::ConnectionStatusEnum::kConnected));
            }
        }
        // cleanup the provisioning data as it is configured per each connect request
        Instance().ClearStationProvisioningData();
    });

    if (CHIP_NO_ERROR == err)
    {
        // the ownership has been transferred to the worker thread - release the buffer
        data.release();
    }
}

void WiFiManager::DisconnectHandler(Platform::UniquePtr<uint8_t> data, size_t length)
{
    // Validate that input data size matches the expected one.
    VerifyOrReturn(length == sizeof(wifi_status));

    CHIP_ERROR err = SystemLayer().ScheduleLambda([capturedData = data.get()] {
        Platform::UniquePtr<uint8_t> safePtr(capturedData);
        uint8_t * rawData          = safePtr.get();
        const wifi_status * status = reinterpret_cast<const wifi_status *>(rawData);
        uint16_t reason;

        switch (status->disconn_reason)
        {
        case WIFI_REASON_DISCONN_UNSPECIFIED:
            reason = WLAN_REASON_UNSPECIFIED;
            break;
        case WIFI_REASON_DISCONN_USER_REQUEST:
            reason = WLAN_REASON_DEAUTH_LEAVING;
            break;
        case WIFI_REASON_DISCONN_AP_LEAVING:
            reason = WLAN_REASON_DEAUTH_LEAVING;
            break;
        case WIFI_REASON_DISCONN_INACTIVITY:
            reason = WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY;
            break;
        default:
            reason = WLAN_REASON_UNSPECIFIED;
            break;
        }
        Instance().SetLastDisconnectReason(reason);

        ChipLogProgress(DeviceLayer, "WiFi station disconnected");
        Instance().mWiFiState = WIFI_STATE_DISCONNECTED;
        Instance().PostConnectivityStatusChange(kConnectivity_Lost);

        WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();
        if (delegate)
        {
            delegate->OnConnectionStatusChanged(
                to_underlying(app::Clusters::WiFiNetworkDiagnostics::ConnectionStatusEnum::kNotConnected));
            delegate->OnDisconnectionDetected(reason);
        }
    });

    if (CHIP_NO_ERROR == err)
    {
        // the ownership has been transferred to the worker thread - release the buffer
        data.release();
    }
}

void WiFiManager::IPv6AddressChangeHandler(const void * data)
{
    const in6_addr * addr = reinterpret_cast<const in6_addr *>(data);

    // Filter out link-local addresses that are not routable outside of a local network.
    if (!net_ipv6_is_ll_addr(addr))
    {
        // This is needed to send mDNS queries containing updated IPv6 addresses.
        ChipDeviceEvent event;
        event.Type = DeviceEventType::kDnssdRestartNeeded;

        CHIP_ERROR error = PlatformMgr().PostEvent(&event);
        if (error != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "Cannot post event: %" CHIP_ERROR_FORMAT, error.Format());
        }
    }
}

WiFiManager::StationStatus WiFiManager::GetStationStatus() const
{
    return WiFiManager::sStatusMap[mWiFiState];
}

void WiFiManager::PostConnectivityStatusChange(ConnectivityChange changeType)
{
    ChipDeviceEvent networkEvent{};
    networkEvent.Type                          = DeviceEventType::kWiFiConnectivityChange;
    networkEvent.WiFiConnectivityChange.Result = changeType;
    PlatformMgr().PostEventOrDie(&networkEvent);
}

void WiFiManager::Recover(System::Layer *, void *)
{
    // Prevent scheduling recovery if we are already connected to the network.
    if (Instance().mWiFiState == WIFI_STATE_COMPLETED)
    {
        Instance().AbortConnectionRecovery();
        return;
    }

    // If kConnectionRecoveryMaxOverallInterval has a non-zero value prevent endless re-scan.
    if (0 != kConnectionRecoveryMaxRetries && (++Instance().mConnectionRecoveryCounter >= kConnectionRecoveryMaxRetries))
    {
        Instance().AbortConnectionRecovery();
        return;
    }

    Instance().Scan(Instance().mWantedNetwork.GetSsidSpan(), nullptr, nullptr, true /* internal scan */);
}

void WiFiManager::ResetRecoveryTime()
{
    mConnectionRecoveryTimeMs  = kConnectionRecoveryMinIntervalMs;
    mConnectionRecoveryCounter = 0;
}

void WiFiManager::AbortConnectionRecovery()
{
    DeviceLayer::SystemLayer().CancelTimer(Recover, nullptr);
    Instance().ResetRecoveryTime();
}

System::Clock::Milliseconds32 WiFiManager::CalculateNextRecoveryTime()
{
    if (mConnectionRecoveryTimeMs > kConnectionRecoveryMaxIntervalMs)
    {
        // Find the new random jitter value in range [-jitter, +jitter].
        int32_t jitter            = chip::Crypto::GetRandU32() % (2 * jitter + 1) - jitter;
        mConnectionRecoveryTimeMs = kConnectionRecoveryMaxIntervalMs + jitter;
        return System::Clock::Milliseconds32(mConnectionRecoveryTimeMs);
    }
    else
    {
        uint32_t currentRecoveryTimeout = mConnectionRecoveryTimeMs;
        mConnectionRecoveryTimeMs       = mConnectionRecoveryTimeMs * 2;
        return System::Clock::Milliseconds32(currentRecoveryTimeout);
    }
}

CHIP_ERROR WiFiManager::SetLowPowerMode(bool onoff)
{
    VerifyOrReturnError(nullptr != mNetIf, CHIP_ERROR_INTERNAL);

    wifi_ps_config currentConfig{};
    if (net_mgmt(NET_REQUEST_WIFI_PS_CONFIG, mNetIf, &currentConfig, sizeof(currentConfig)))
    {
        ChipLogError(DeviceLayer, "Get current low power mode config request failed");
        return CHIP_ERROR_INTERNAL;
    }

    if ((currentConfig.ps_params.enabled == WIFI_PS_ENABLED && onoff == false) ||
        (currentConfig.ps_params.enabled == WIFI_PS_DISABLED && onoff == true))
    {
        wifi_ps_params params{ .enabled = onoff ? WIFI_PS_ENABLED : WIFI_PS_DISABLED };
        if (net_mgmt(NET_REQUEST_WIFI_PS, mNetIf, &params, sizeof(params)))
        {
            ChipLogError(DeviceLayer, "Set low power mode request failed");
            return CHIP_ERROR_INTERNAL;
        }
        ChipLogProgress(DeviceLayer, "Successfully set low power mode [%d]", onoff);
        return CHIP_NO_ERROR;
    }

    ChipLogDetail(DeviceLayer, "Low power mode is already in requested state [%d]", onoff);
    return CHIP_NO_ERROR;
}

void WiFiManager::SetLastDisconnectReason(uint16_t reason)
{
    mLastDisconnectedReason = reason;
}

uint16_t WiFiManager::GetLastDisconnectReason()
{
    return mLastDisconnectedReason;
}

} // namespace DeviceLayer
} // namespace chip
