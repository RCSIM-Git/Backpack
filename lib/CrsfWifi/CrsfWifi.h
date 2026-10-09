#pragma once
#include <WiFiUdp.h>
#include "CrsfWifiSession.h"
#include "msp.h"
#include "msptypes.h"

// Experimental trainer input. Disabled unless explicitly selected at build time.
class CrsfWifi {
    WiFiUDP udp;
    crsfwifi::Session session;
    bool listening=false;
    uint32_t telemetrySequence=0;
public:
    void disable() {
        session.clear();
        if (listening) { udp.stop(); listening=false; }
    }
    void poll(uint32_t now, bool enabled, MSP &msp) {
        if (!enabled || WiFi.getMode()==WIFI_OFF) {
            disable();
            return;
        }
        if (!listening) listening=udp.begin(8888)==1;
        if (!listening) return;
        session.active(now);
        uint16_t channels[16];
        bool updated=false;
        // Bound work per loop so serial processing cannot be starved by Wi-Fi.
        for (unsigned i=0; i<4; ++i) {
            int size=udp.parsePacket();
            if (!size) break;
            uint8_t packet[crsfwifi::HeaderSize+crsfwifi::RcSize];
            IPAddress remote=udp.remoteIP(); uint16_t port=udp.remotePort();
            if (size!=int(sizeof(packet)) && size!=int(crsfwifi::HeaderSize)) { udp.flush(); continue; }
            int count=udp.read(packet,sizeof(packet));
            if (count!=size || !session.accept(packet,count,uint32_t(remote),port,now,channels)) continue;
            updated=true;
        }
        // Release received after an RC packet cancels that packet as well.
        if (updated && session.active(now)) {
            mspPacket_t ptr; ptr.reset(); ptr.makeCommand(); ptr.function=MSP_ELRS_BACKPACK_SET_PTR;
            for (uint16_t channel : channels) { ptr.addByte(uint8_t(channel)); ptr.addByte(uint8_t(channel>>8)); }
            msp.sendPacket(&ptr,&Serial);
        }
    }
    void telemetry(const uint8_t *frame,size_t size,uint32_t now) {
        if (!listening || !session.active(now) || !crsfwifi::validFrame(frame,size)) return;
        uint8_t packet[crsfwifi::HeaderSize+64];
        crsfwifi::header(packet,3,session.id,telemetrySequence++);
        memcpy(packet+crsfwifi::HeaderSize,frame,size);
        if (udp.beginPacket(IPAddress(session.ip),session.port)) {
            udp.write(packet,crsfwifi::HeaderSize+size); udp.endPacket();
        }
    }
};
