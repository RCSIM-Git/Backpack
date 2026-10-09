#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace crsfwifi {
constexpr size_t HeaderSize = 16;
constexpr size_t RcSize = 26;
constexpr uint32_t LeaseMs = 300;
inline uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void write32(uint8_t *p, uint32_t n) {
    for (unsigned i=0; i<4; ++i) p[i] = uint8_t(n >> (8*i));
}
inline uint8_t crc(const uint8_t *p, size_t n) {
    uint8_t result=0;
    while (n--) {
        result ^= *p++;
        for (unsigned i=0; i<8; ++i) result = result & 128 ? uint8_t((result << 1)^0xd5) : uint8_t(result << 1);
    }
    return result;
}
inline bool validFrame(const uint8_t *p, size_t n) {
    return n>=4 && n<=64 && size_t(p[1])+2==n && crc(p+2,n-3)==p[n-1];
}
inline void header(uint8_t *p, uint8_t kind, uint32_t session, uint32_t sequence) {
    memcpy(p,"CRWF",4); p[4]=1; p[5]=kind; p[6]=p[7]=0;
    write32(p+8,session); write32(p+12,sequence);
}
class Session {
public:
    uint32_t ip=0, id=0, sequence=0, last=0;
    uint16_t port=0;
    bool claimed=false;
    uint32_t retired[16]={};
    unsigned retiredNext=0;
    void clear() {
        if (claimed) { retired[retiredNext]=id; retiredNext=(retiredNext+1)%16; }
        claimed=false;
    }
    bool active(uint32_t now) {
        if (claimed && uint32_t(now-last)>=LeaseMs) clear();
        return claimed;
    }
    bool accept(const uint8_t *p,size_t n,uint32_t peer,uint16_t peerPort,uint32_t now,uint16_t channels[16]) {
        active(now);
        if (n<HeaderSize || memcmp(p,"CRWF",4) || p[4]!=1 || p[6] || p[7]) return false;
        uint32_t token=read32(p+8), seq=read32(p+12);
        if (!token || !peerPort) return false;
        for (uint32_t old : retired) if (old==token) return false;
        if (p[5]!=1 && p[5]!=2) return false;
        if (p[5]==1) {
            if (n!=HeaderSize+RcSize) return false;
            const uint8_t *f=p+HeaderSize;
            if ((f[0]!=0xea && f[0]!=0xc8) || f[1]!=24 || f[2]!=0x16 || !validFrame(f,RcSize)) return false;
        } else if (n!=HeaderSize || !claimed) return false;
        if (claimed) {
            uint32_t delta=seq-sequence;
            if (ip!=peer || port!=peerPort || id!=token || !delta || delta>=0x80000000u) return false;
        }
        if (p[5]==2) { clear(); return false; }
        const uint8_t *bits=p+HeaderSize+3;
        for (unsigned ch=0; ch<16; ++ch) {
            unsigned bit=ch*11, offset=bit/8, shift=bit%8;
            uint32_t word=uint32_t(bits[offset]) | uint32_t(bits[offset+1])<<8;
            if (offset+2<22) word |= uint32_t(bits[offset+2])<<16;
            channels[ch]=uint16_t((word>>shift)&2047);
        }
        ip=peer; port=peerPort; id=token; sequence=seq; last=now; claimed=true;
        return true;
    }
};
}
