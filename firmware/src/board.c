#include "board.h"
#include <string.h>

uint32_t SystemCoreClock=BOARD_CLOCK_HZ;
static volatile uint32_t ticks, io_errors;
static bool adc_ready;
typedef struct { uint8_t data[256]; volatile uint16_t head,tail; } rx_ring_t;
static rx_ring_t bt_rx, co2_rx;
static uint8_t bt_tx[1024];
static volatile uint16_t tx_head,tx_tail;

static void pin(GPIO_TypeDef *port, unsigned pin_number, unsigned config) {
    volatile uint32_t *reg=pin_number<8 ? &port->CRL : &port->CRH;
    unsigned shift=(pin_number&7u)*4;
    *reg=(*reg & ~(15u<<shift)) | (config<<shift);
}
static bool wait_bit(volatile uint32_t *reg,uint32_t mask,bool set,uint32_t loops) {
    while (loops--) if (((*reg&mask)!=0)==set) return true;
    return false;
}
void SystemInit(void) {
    RCC->CR |= RCC_CR_HSION;
    RCC->CFGR=0;
    RCC->CR &= ~(RCC_CR_PLLON|RCC_CR_HSEON|RCC_CR_CSSON);
    FLASH->ACR=FLASH_ACR_PRFTBE|FLASH_ACR_LATENCY_2;
    /* HSI/2 x 16 = 64 MHz; APB1 = 32 MHz; ADC = 10.67 MHz. */
    RCC->CFGR=RCC_CFGR_PLLMULL16|RCC_CFGR_PPRE1_DIV2|RCC_CFGR_ADCPRE_DIV6;
    RCC->CR |= RCC_CR_PLLON;
    if (!wait_bit(&RCC->CR,RCC_CR_PLLRDY,true,2000000)) while (1) { }
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    if (!wait_bit(&RCC->CFGR,RCC_CFGR_SWS_PLL,true,2000000)) while (1) { }
    SCB->VTOR=FLASH_BASE;
}
void SystemCoreClockUpdate(void) { SystemCoreClock=BOARD_CLOCK_HZ; }
uint32_t board_millis(void) { return ticks; }
void SysTick_Handler(void) { ++ticks; }
void board_delay_us(uint32_t us) {
    uint32_t start=DWT->CYCCNT;
    uint32_t cycles=us*(BOARD_CLOCK_HZ/1000000u);
    while (DWT->CYCCNT-start<cycles) { __NOP(); }
}
static void uart_setup(USART_TypeDef *u,uint32_t clock) {
    u->BRR=(clock+4800u)/9600u;
    u->CR1=USART_CR1_UE|USART_CR1_TE|USART_CR1_RE|USART_CR1_RXNEIE;
    u->CR3=USART_CR3_EIE;
}
void board_init(void) {
    RCC->APB2ENR=RCC_APB2ENR_AFIOEN|RCC_APB2ENR_IOPAEN|RCC_APB2ENR_IOPBEN|
        RCC_APB2ENR_IOPCEN|RCC_APB2ENR_ADC1EN|RCC_APB2ENR_USART1EN;
    RCC->APB1ENR=RCC_APB1ENR_USART2EN|RCC_APB1ENR_TIM3EN|RCC_APB1ENR_PWREN|RCC_APB1ENR_BKPEN;
    AFIO->MAPR=AFIO_MAPR_SWJ_CFG_JTAGDISABLE; /* SWD PA13/PA14 remains available. */
    /* Active-high loads must have external 10k gate pull-downs. Set LOW before output mode. */
    GPIOB->BRR=(1u<<0)|(1u<<1)|(1u<<8)|(1u<<9);
    pin(GPIOB,0,2); pin(GPIOB,1,2); pin(GPIOB,8,2); pin(GPIOB,9,2);
    pin(GPIOA,0,0); pin(GPIOA,1,0);
    GPIOB->BSRR=1u<<5; pin(GPIOB,5,6); /* Open-drain 1-Wire, external 4.7k to 3V3. */
    GPIOB->BSRR=(1u<<6)|(1u<<7); pin(GPIOB,6,6); pin(GPIOB,7,6);
    for (unsigned k=12;k<=15;++k) { GPIOB->BSRR=1u<<k; pin(GPIOB,k,8); }
    GPIOA->BSRR=1u<<8; pin(GPIOA,8,8);
    pin(GPIOA,9,0xb); pin(GPIOA,10,4); pin(GPIOA,2,0xb); pin(GPIOA,3,4);
    uart_setup(USART1,64000000); uart_setup(USART2,32000000);
    NVIC_SetPriority(USART1_IRQn,2); NVIC_SetPriority(USART2_IRQn,2);
    NVIC_EnableIRQ(USART1_IRQn); NVIC_EnableIRQ(USART2_IRQn);
    TIM3->PSC=63; TIM3->ARR=19999; TIM3->CCR1=SERVO_OPEN_US;
    TIM3->CCMR1=(6u<<4)|TIM_CCMR1_OC1PE; TIM3->CCER=TIM_CCER_CC1E;
    TIM3->EGR=TIM_EGR_UG; TIM3->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
    pin(GPIOA,6,0xa);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT=0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    SysTick_Config(BOARD_CLOCK_HZ/1000u); NVIC_SetPriority(SysTick_IRQn,1);
    ADC1->SMPR2=(7u<<0)|(7u<<3); ADC1->CR2=ADC_CR2_ADON;
    board_delay_us(10); ADC1->CR2 |= ADC_CR2_RSTCAL;
    adc_ready=wait_bit(&ADC1->CR2,ADC_CR2_RSTCAL,false,100000);
    ADC1->CR2 |= ADC_CR2_CAL;
    adc_ready=adc_ready && wait_bit(&ADC1->CR2,ADC_CR2_CAL,false,100000);
    /* ~2 seconds nominal; LSI tolerance means this is not a precise timeout. */
    IWDG->KR=0x5555; IWDG->PR=4; IWDG->RLR=1249;
    (void)wait_bit(&IWDG->SR,IWDG_SR_PVU|IWDG_SR_RVU,false,100000);
    IWDG->KR=0xaaaa; IWDG->KR=0xcccc;
}
void board_watchdog(void) { IWDG->KR=0xaaaa; }
bool board_adc(uint8_t channel,uint16_t *value) {
    if (!adc_ready || channel>1) return false;
    ADC1->SQR1=0; ADC1->SQR3=channel;
    ADC1->SR=0;
    ADC1->CR2=ADC_CR2_ADON|ADC_CR2_EXTTRIG|ADC_CR2_EXTSEL|ADC_CR2_SWSTART;
    if (!wait_bit(&ADC1->SR,ADC_SR_EOC,true,100000)) { ++io_errors; return false; }
    *value=(uint16_t)ADC1->DR; return true;
}
void board_apply(uint8_t outputs,bool alarm) {
    uint32_t bits=(outputs&OUT_PUMP?1u<<0:0)|(outputs&OUT_FAN?1u<<1:0)|
        (outputs&OUT_LAMP?1u<<8:0)|(alarm?1u<<9:0);
    uint32_t mask=(1u<<0)|(1u<<1)|(1u<<8)|(1u<<9);
    GPIOB->BSRR=bits|((mask&~bits)<<16);
    TIM3->CCR1=outputs&OUT_SHADE?SERVO_SHADE_US:SERVO_OPEN_US;
}
bool board_water_ok(void) { return (GPIOB->IDR&(1u<<12))==0; }
uint8_t board_keys(void) {
    return (uint8_t)(((GPIOB->IDR&(1u<<13))?0:1)|((GPIOB->IDR&(1u<<14))?0:2)|
        ((GPIOB->IDR&(1u<<15))?0:4)|((GPIOA->IDR&(1u<<8))?0:8));
}
static void uart_rx(USART_TypeDef *u,rx_ring_t *r) {
    uint32_t status=u->SR;
    if (status&(USART_SR_RXNE|USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) {
        uint8_t b=(uint8_t)u->DR;
        if (status&(USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) { ++io_errors; return; }
        uint16_t next=(uint16_t)((r->head+1u)&255u);
        if (next==r->tail) ++io_errors;
        else { r->data[r->head]=b; r->head=next; }
    }
}
void USART1_IRQHandler(void) {
    uart_rx(USART1,&bt_rx);
    if ((USART1->SR&USART_SR_TXE) && (USART1->CR1&USART_CR1_TXEIE)) {
        if (tx_tail!=tx_head) { USART1->DR=bt_tx[tx_tail]; tx_tail=(uint16_t)((tx_tail+1u)&1023u); }
        else USART1->CR1 &= ~USART_CR1_TXEIE;
    }
}
void USART2_IRQHandler(void) { uart_rx(USART2,&co2_rx); }
static bool pop(rx_ring_t *r,uint8_t *b) {
    if (r->head==r->tail) return false;
    *b=r->data[r->tail]; r->tail=(uint16_t)((r->tail+1u)&255u); return true;
}
bool board_bt_read(uint8_t *byte) { return pop(&bt_rx,byte); }
bool board_co2_read(uint8_t *byte) { return pop(&co2_rx,byte); }
bool board_bt_write(const char *text) {
    size_t n=strlen(text);
    uint32_t primask=__get_PRIMASK(); __disable_irq();
    size_t free_bytes=(tx_tail-tx_head-1u)&1023u;
    if (n>free_bytes) { ++io_errors; __set_PRIMASK(primask); return false; }
    for (size_t i=0;i<n;++i) { bt_tx[tx_head]=(uint8_t)text[i]; tx_head=(uint16_t)((tx_head+1u)&1023u); }
    USART1->CR1 |= USART_CR1_TXEIE; __set_PRIMASK(primask); return true;
}
bool board_co2_query(void) {
    static const uint8_t command[9]={0xff,0x01,0x86,0,0,0,0,0,0x79};
    for (unsigned i=0;i<9;++i) {
        if (!wait_bit(&USART2->SR,USART_SR_TXE,true,100000)) { ++io_errors; return false; }
        USART2->DR=command[i];
    }
    return true;
}
uint32_t board_io_errors(void) { return io_errors; }
static bool ow_reset(void) {
    GPIOB->BRR=1u<<5; board_delay_us(500);
    uint32_t primask=__get_PRIMASK(); __disable_irq();
    GPIOB->BSRR=1u<<5; board_delay_us(70);
    bool presence=(GPIOB->IDR&(1u<<5))==0;
    __set_PRIMASK(primask); board_delay_us(410);
    return presence && (GPIOB->IDR&(1u<<5))!=0;
}
static void ow_write(uint8_t b) {
    for (unsigned bit=0;bit<8;++bit) {
        uint32_t primask=__get_PRIMASK(); __disable_irq();
        GPIOB->BRR=1u<<5; board_delay_us(b&1u?6:60); GPIOB->BSRR=1u<<5;
        board_delay_us(b&1u?64:10); __set_PRIMASK(primask); b>>=1;
    }
}
static uint8_t ow_read(void) {
    uint8_t b=0;
    for (unsigned bit=0;bit<8;++bit) {
        uint32_t primask=__get_PRIMASK(); __disable_irq();
        GPIOB->BRR=1u<<5; board_delay_us(3); GPIOB->BSRR=1u<<5; board_delay_us(10);
        if (GPIOB->IDR&(1u<<5)) b |= (uint8_t)(1u<<bit);
        board_delay_us(57); __set_PRIMASK(primask);
    }
    return b;
}
bool board_temperature_start(void) {
    if (!ow_reset()) return false;
    ow_write(0xcc); ow_write(0x44); return true;
}
bool board_temperature_read(int16_t *temp_dC) {
    /* External power only. A zero completion bit means conversion did not finish. */
    if (ow_read()!=0xff || !ow_reset()) return false;
    uint8_t scratch[9]; ow_write(0xcc); ow_write(0xbe);
    for (unsigned i=0;i<9;++i) scratch[i]=ow_read();
    return ds18_decode(scratch,temp_dC);
}
void HardFault_Handler(void) {
    GPIOB->BRR=(1u<<0)|(1u<<1)|(1u<<8)|(1u<<9);
    NVIC_SystemReset();
}
void MemManage_Handler(void) { HardFault_Handler(); }
void BusFault_Handler(void) { HardFault_Handler(); }
void UsageFault_Handler(void) { HardFault_Handler(); }
