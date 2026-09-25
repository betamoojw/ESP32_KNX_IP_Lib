#pragma once

#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <atomic>
#include <esp-knx-ip.h>

// Owns network policy; the KNX client remains independent of Wi-Fi/Ethernet drivers.
class NetworkConnection {
public:
    explicit NetworkConnection(ESPKNXIP &client) : client_(client) {}

    void begin() {
        Network.begin();
        Network.setHostname("esp-knx-test");
        Network.onEvent([this](arduino_event_id_t event, arduino_event_info_t) {
            // Network callbacks run on another task. Only signal here; sockets belong to loop().
            switch (event) {
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                changed_.fetch_or(WifiChanged);
                break;
            case ARDUINO_EVENT_ETH_DISCONNECTED:
            case ARDUINO_EVENT_ETH_LOST_IP:
            case ARDUINO_EVENT_ETH_GOT_IP:
                changed_.fetch_or(EthernetChanged);
                break;
            default: break;
            }
        });
        WiFi.setAutoReconnect(true);
        // ETH.begin uses the PHY/pin definitions in platformio.ini.
        if (!ETH.begin()) Serial.println("Ethernet initialization failed: check PHY/pin settings.");
        pinMode(4, INPUT_PULLUP);
        const bool resetCredentials = digitalRead(4) == LOW;
        // Ground GPIO4 during startup to provision again (GPIO0 is the RMII clock).
        WiFiProv.beginProvision(NETWORK_PROV_SCHEME_SOFTAP, NETWORK_PROV_SCHEME_HANDLER_NONE,
                                NETWORK_PROV_SECURITY_1, "knx-setup", "PROV_KNX", "knx-setup",
                                nullptr, resetCredentials);
        Serial.println("For provisioning: Espressif provisioning app, SoftAP PROV_KNX, password/PoP knx-setup.");
    }

    bool update() {
        NetworkInterface *selected = usable(WiFi.STA) ? &WiFi.STA :
                                     usable(ETH) ? static_cast<NetworkInterface *>(&ETH) : nullptr;
        const IPAddress address = selected ? selected->localIP() : IPAddress();
        const unsigned events = changed_.exchange(0);
        const bool changed = active_ && (events & (active_ == &WiFi.STA ? WifiChanged : EthernetChanged));
        if (changed || selected != active_ || address != address_ ||
            (selected && Network.getDefaultInterface() != selected)) {
            client_.stop();
            connected_ = false;
            attempted_ = false;
            active_ = selected;
            address_ = address;
        }
        if (!active_) return false;
        const uint32_t now = millis();
        if (!connected_ && (!attempted_ || uint32_t(now - attemptedAt_) >= 2000)) {
            attempted_ = true;
            attemptedAt_ = now;
            if (Network.setDefaultInterface(*active_))
                connected_ = client_.start_routing() == knxip::Result::Ok;
            if (connected_) {
                Serial.print(active_ == &WiFi.STA ? "KNX routing on Wi-Fi: " : "KNX routing on Ethernet: ");
                Serial.println(address_);
            }
        }
        return connected_;
    }

private:
    static bool usable(NetworkInterface &interface) {
        return interface.connected() && interface.hasIP() && uint32_t(interface.localIP()) != 0;
    }
    ESPKNXIP &client_;
    enum { WifiChanged = 1, EthernetChanged = 2 };
    std::atomic<unsigned> changed_{0};
    NetworkInterface *active_ = nullptr;
    IPAddress address_;
    bool connected_ = false, attempted_ = false;
    uint32_t attemptedAt_ = 0;
};
