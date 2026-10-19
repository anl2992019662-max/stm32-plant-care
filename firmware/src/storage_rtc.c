#include "board.h"
#include <string.h>

#define SLOT_A 0x0800f800u
#define SLOT_B 0x0800fc00u
static uint32_t active_slot,sequence;
static bool rtc_ready,rtc_lse;

bool board_config_load(plant_config_t *cfg) {
    plant_config_t a,b; uint32_t sa=0,sb=0;
    bool va=config_unpack((const uint8_t *)SLOT_A,&a,&sa);
    bool vb=config_unpack((const uint8_t *)SLOT_B,&b,&sb);
    if (va && (!vb || (int32_t)(sa-sb)>0)) { *cfg=a; sequence=sa; active_slot=SLOT_A; return true; }
    if (vb) { *cfg=b; sequence=sb; active_slot=SLOT_B; return true; }
    active_slot=0; sequence=0; plant_defaults(cfg); return false;
}
static bool flash_wait(void) {
    uint32_t start=DWT->CYCCNT;
    while (FLASH->SR&FLASH_SR_BSY) if (DWT->CYCCNT-start>BOARD_CLOCK_HZ/5u) return false;
    return !(FLASH->SR&(FLASH_SR_PGERR|FLASH_SR_WRPRTERR));
}
static bool program_half(uint32_t address,uint16_t value) {
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint16_t *)address=value;
    bool ok=flash_wait(); FLASH->CR &= ~FLASH_CR_PG;
    return ok && *(volatile uint16_t *)address==value;
}
bool board_config_save(const plant_config_t *cfg) {
    if (!plant_config_valid(cfg)) return false;
    uint8_t record[RECORD_BYTES]; config_pack(record,cfg,sequence+1);
    uint32_t target=active_slot==SLOT_A?SLOT_B:SLOT_A;
    board_watchdog();
    FLASH->KEYR=0x45670123u; FLASH->KEYR=0xcdef89abu;
    FLASH->SR=FLASH_SR_PGERR|FLASH_SR_WRPRTERR|FLASH_SR_EOP;
    bool ok=flash_wait();
    if (ok) {
        FLASH->CR |= FLASH_CR_PER; FLASH->AR=target; FLASH->CR |= FLASH_CR_STRT;
        ok=flash_wait(); FLASH->CR &= ~FLASH_CR_PER;
    }
    /* Commit magic is programmed last; the old slot survives a failed save. */
    for (unsigned i=4;ok && i<RECORD_BYTES;i+=2)
        ok=program_half(target+i,(uint16_t)((uint16_t)record[i]|((uint16_t)record[i+1]<<8)));
    for (unsigned i=0;ok && i<4;i+=2)
        ok=program_half(target+i,(uint16_t)((uint16_t)record[i]|((uint16_t)record[i+1]<<8)));
    FLASH->CR |= FLASH_CR_LOCK;
    plant_config_t verify; uint32_t read_sequence;
    if (ok) ok=config_unpack((const uint8_t *)target,&verify,&read_sequence) &&
        read_sequence==sequence+1 && memcmp(&verify,cfg,sizeof(verify))==0;
    if (ok) { active_slot=target; ++sequence; }
    board_watchdog(); return ok;
}
static bool rtc_wait(uint32_t mask) {
    uint32_t start=board_millis();
    while (!(RTC->CRL&mask)) {
        board_watchdog();
        if (board_millis()-start>=1000) return false;
    }
    return true;
}
void board_rtc_init(void) {
    PWR->CR |= PWR_CR_DBP;
    if ((RCC->BDCR&RCC_BDCR_RTCEN) && BKP->DR2==0x51c3) {
        rtc_lse=(RCC->BDCR&RCC_BDCR_RTCSEL)==RCC_BDCR_RTCSEL_LSE;
    } else {
        RCC->BDCR |= RCC_BDCR_BDRST; RCC->BDCR &= ~RCC_BDCR_BDRST;
        RCC->BDCR |= RCC_BDCR_LSEON;
        uint32_t start=board_millis();
        while (!(RCC->BDCR&RCC_BDCR_LSERDY) && board_millis()-start<1500) board_watchdog();
        rtc_lse=(RCC->BDCR&RCC_BDCR_LSERDY)!=0;
        if (!rtc_lse) {
            RCC->CSR |= RCC_CSR_LSION;
            start=board_millis();
            while (!(RCC->CSR&RCC_CSR_LSIRDY)) {
                board_watchdog(); if (board_millis()-start>100) return;
            }
        }
        RCC->BDCR=(RCC->BDCR&~RCC_BDCR_RTCSEL)|RCC_BDCR_RTCEN|
            (rtc_lse?RCC_BDCR_RTCSEL_LSE:RCC_BDCR_RTCSEL_LSI);
        RTC->CRL &= ~RTC_CRL_RSF;
        if (!rtc_wait(RTC_CRL_RSF)) return;
        if (!rtc_wait(RTC_CRL_RTOFF)) return;
        RTC->CRL |= RTC_CRL_CNF;
        RTC->PRLH=0; RTC->PRLL=rtc_lse?32767u:39999u;
        RTC->CNTH=0; RTC->CNTL=0;
        RTC->CRL &= ~RTC_CRL_CNF;
        if (!rtc_wait(RTC_CRL_RTOFF)) return;
        BKP->DR2=0x51c3;
    }
    RTC->CRL &= ~RTC_CRL_RSF;
    rtc_ready=rtc_wait(RTC_CRL_RSF);
}
uint32_t board_rtc_get(void) {
    if (!rtc_ready || BKP->DR1!=0x71ae) return 0;
    uint32_t high,low;
    do { high=RTC->CNTH; low=RTC->CNTL; } while (high!=RTC->CNTH);
    return (high<<16)|low;
}
bool board_rtc_set(uint32_t epoch) {
    if (!rtc_ready || epoch<1577836800u || epoch>4102444799u || !rtc_wait(RTC_CRL_RTOFF)) return false;
    RTC->CRL |= RTC_CRL_CNF; RTC->CNTH=epoch>>16; RTC->CNTL=epoch&65535u;
    RTC->CRL &= ~RTC_CRL_CNF;
    if (!rtc_wait(RTC_CRL_RTOFF)) return false;
    BKP->DR1=0x71ae; return true;
}
bool board_rtc_precise(void) { return rtc_ready && rtc_lse; }
