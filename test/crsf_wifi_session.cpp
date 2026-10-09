#include "../lib/CrsfWifi/CrsfWifiSession.h"
#include <assert.h>

static void rc(uint8_t p[42],uint32_t token,uint32_t seq) {
    crsfwifi::header(p,1,token,seq);
    uint8_t *f=p+16; f[0]=0xea; f[1]=24; f[2]=0x16;
    memset(f+3,0,22);
    for (unsigned ch=0;ch<16;++ch) {
        unsigned value=172+ch*100;
        for (unsigned b=0;b<11;++b) if (value&(1u<<b)) f[3+(ch*11+b)/8] |= 1u<<((ch*11+b)%8);
    }
    f[25]=crsfwifi::crc(f+2,23);
}
int main() {
    crsfwifi::Session s; uint16_t channels[16]; uint8_t p[42];
    rc(p,123,1);
    assert(s.accept(p,42,1,8888,10,channels));
    for (unsigned ch=0;ch<16;++ch) assert(channels[ch]==172+ch*100);
    assert(!s.accept(p,42,1,8888,20,channels)); // duplicate
    rc(p,123,2);
    assert(!s.accept(p,42,2,8888,20,channels)); // endpoint ownership
    assert(!s.accept(p,42,1,9999,20,channels));
    p[41]^=1;
    assert(!s.accept(p,42,1,8888,20,channels)); // CRC
    p[41]^=1;
    assert(!s.accept(p,41,1,8888,20,channels));
    assert(s.accept(p,42,1,8888,20,channels));
    assert(!s.active(320));
    assert(!s.accept(p,42,1,8888,321,channels)); // retired session
    rc(p,321,1); assert(s.accept(p,42,1,8888,321,channels));
    s.clear(); rc(p,123,3); assert(!s.accept(p,42,1,8888,321,channels)); // A/B/A replay
    rc(p,124,0xffffffffu); assert(s.accept(p,42,1,8888,321,channels));
    rc(p,124,0); assert(s.accept(p,42,1,8888,322,channels)); // sequence wrap
    crsfwifi::header(p,2,124,1);
    assert(!s.accept(p,16,1,8888,323,channels)); assert(!s.claimed);
    rc(p,125,1); assert(s.accept(p,42,1,8888,0xfffffff0u,channels));
    assert(s.active(5)); assert(!s.active(300)); // clock wrap
    for (unsigned n=0;n<42;++n) assert(!s.accept(p,n,1,8888,301,channels));
    p[6]=1; assert(!s.accept(p,42,1,8888,301,channels));
    return 0;
}
