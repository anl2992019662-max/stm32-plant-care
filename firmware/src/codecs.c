#include "plant.h"
#include <string.h>
_Static_assert(sizeof(plant_config_t)==68,"Update the serialized format when changing config fields");

uint16_t median5(const uint16_t values[5]) {
    uint16_t a[5]; memcpy(a,values,sizeof(a));
    for (unsigned i=1;i<5;++i) for (unsigned j=i;j>0 && a[j]<a[j-1];--j) {
        uint16_t swap=a[j]; a[j]=a[j-1]; a[j-1]=swap;
    }
    return a[2];
}
bool adc_percent(uint16_t raw, uint32_t zero, uint32_t hundred, uint16_t *percent) {
    if (zero>4095 || hundred>4095 || zero==hundred || raw<20 || raw>4075) return false;
    int32_t value=((int32_t)raw-(int32_t)zero)*100/((int32_t)hundred-(int32_t)zero);
    *percent=(uint16_t)(value<0?0:(value>100?100:value)); return true;
}
uint8_t crc8_dallas(const uint8_t *data, size_t length) {
    uint8_t crc=0;
    for (size_t i=0;i<length;++i) {
        uint8_t b=data[i];
        for (unsigned j=0;j<8;++j) { uint8_t mix=(crc^b)&1u; crc>>=1; if (mix) crc^=0x8cu; b>>=1; }
    }
    return crc;
}
uint32_t crc32_ieee(const uint8_t *data, size_t length) {
    uint32_t crc=0xffffffffu;
    for (size_t i=0;i<length;++i) {
        crc^=data[i];
        for (unsigned j=0;j<8;++j) crc=(crc>>1)^((crc&1u)?0xedb88320u:0);
    }
    return ~crc;
}
bool ds18_decode(const uint8_t scratch[9], int16_t *temp_dC) {
    if (crc8_dallas(scratch,8)!=scratch[8] || (scratch[4]&0x9fu)!=0x1fu) return false;
    int16_t raw=(int16_t)((uint16_t)scratch[0]|((uint16_t)scratch[1]<<8));
    unsigned resolution=(scratch[4]>>5)&3u;
    raw=(int16_t)((uint16_t)raw & (uint16_t)(0xffffu<<(3u-resolution)));
    if (raw < -55*16 || raw > 125*16) return false;
    /* The driver reads only after a completed conversion, so a real 85 C is valid. */
    *temp_dC=(int16_t)((int32_t)raw*10/16); return true;
}
bool mhz_decode(const uint8_t frame[9], uint16_t *ppm) {
    if (frame[0]!=0xff || frame[1]!=0x86) return false;
    uint8_t sum=0;
    for (unsigned i=1;i<9;++i) sum=(uint8_t)(sum+frame[i]);
    if (sum!=0) return false;
    uint16_t result=(uint16_t)((uint16_t)frame[2]*256u+frame[3]);
    if (result<400 || result>10000) return false;
    *ppm=result; return true;
}
static void put32(uint8_t *b, uint32_t n) { for (unsigned i=0;i<4;++i) b[i]=(uint8_t)(n>>(8*i)); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
void config_pack(uint8_t record[RECORD_BYTES], const plant_config_t *cfg, uint32_t seq) {
    uint32_t values[17]; memcpy(values,cfg,sizeof(values));
    put32(record,0x504c4e54u); put32(record+4,1); put32(record+8,seq);
    for (unsigned i=0;i<17;++i) put32(record+12+4*i,values[i]);
    put32(record+80,crc32_ieee(record+4,76));
}
bool config_unpack(const uint8_t record[RECORD_BYTES], plant_config_t *cfg, uint32_t *seq) {
    if (get32(record)!=0x504c4e54u || get32(record+4)!=1 || get32(record+80)!=crc32_ieee(record+4,76)) return false;
    uint32_t values[17];
    for (unsigned i=0;i<17;++i) values[i]=get32(record+12+4*i);
    plant_config_t candidate; memcpy(&candidate,values,sizeof(candidate));
    if (!plant_config_valid(&candidate)) return false;
    *cfg=candidate; *seq=get32(record+8); return true;
}
