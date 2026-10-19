#include "board.h"
#include <string.h>

static uint8_t pixels[1024];
static void scl(bool high) { GPIOB->BSRR=high?(1u<<6):(1u<<22); }
static void sda(bool high) { GPIOB->BSRR=high?(1u<<7):(1u<<23); }
static bool clock_high(void) {
    scl(true);
    uint32_t start=DWT->CYCCNT;
    while (!(GPIOB->IDR&(1u<<6))) if (DWT->CYCCNT-start>12800u) return false;
    board_delay_us(4); return true;
}
static void stop(void) { scl(false); sda(false); board_delay_us(4); (void)clock_high(); sda(true); board_delay_us(4); }
static bool start(void) {
    sda(true);
    if (!clock_high() || !(GPIOB->IDR&(1u<<7))) return false;
    sda(false); board_delay_us(4); scl(false); return true;
}
static bool byte_write(uint8_t b) {
    for (unsigned i=0;i<8;++i) {
        scl(false); sda((b&0x80)!=0); board_delay_us(4);
        if (!clock_high()) return false;
        b<<=1;
    }
    scl(false); sda(true); board_delay_us(4);
    if (!clock_high()) return false;
    bool ack=(GPIOB->IDR&(1u<<7))==0; scl(false); return ack;
}
static bool send(uint8_t control,const uint8_t *data,size_t n) {
    bool ok=start();
    if (ok) ok=byte_write(0x78) && byte_write(control); /* 7-bit address 0x3C. */
    for (size_t i=0;ok && i<n;++i) ok=byte_write(data[i]);
    stop(); return ok;
}
bool oled_init(void) {
    sda(true);
    for (unsigned i=0;i<9;++i) { scl(false); board_delay_us(4); if (!clock_high()) break; }
    stop();
    /* SSD1306 128x64, internal charge pump; SH1106 is not interchangeable. */
    const uint8_t commands[]={0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0x8d,0x14,
        0x20,0x02,0xa1,0xc8,0xda,0x12,0x81,0x7f,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0xaf};
    oled_clear(); return send(0,commands,sizeof(commands));
}
bool oled_flush_page(uint8_t page) {
    if (page>7) return false;
    uint8_t command[3]={(uint8_t)(0xb0+page),0,0x10};
    return send(0,command,3) && send(0x40,pixels+page*128u,128);
}
void oled_clear(void) { memset(pixels,0,sizeof(pixels)); }
/* Original 3x5 block font, rows encoded as three bits; no external font asset. */
static const char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.:/%+?=,";
static const uint16_t shapes[]={
    0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf,
    0x2bed,0x6bae,0x7927,0x6b6e,0x79e7,0x79e4,0x796f,0x5bed,0x7497,0x126f,
    0x5bad,0x4927,0x5fed,0x5f7d,0x7b6f,0x7be4,0x7b7b,0x7bea,0x79cf,0x7492,
    0x5b6f,0x5b6a,0x5b7d,0x5aad,0x5a92,0x72a7,0x01c0,0x0002,0x0410,0x12a4,
    0x52a5,0x05d0,0x7282,0x0e38,0x0014
};
void oled_text(uint8_t row,const char *text) {
    if (row>7) return;
    uint8_t *line=pixels+128u*row; memset(line,0,128);
    for (unsigned x=0;x<32 && text[x];++x) {
        char c=text[x]; if (c>='a' && c<='z') c=(char)(c-'a'+'A');
        const char *found=strchr(alphabet,c); uint16_t shape=found?shapes[found-alphabet]:0;
        for (unsigned col=0;col<3;++col) {
            uint8_t v=0;
            for (unsigned y=0;y<5;++y) if (shape&(1u<<(14u-y*3u-col))) v|=(uint8_t)(1u<<(y+1));
            line[x*4+col]=v;
        }
    }
}
