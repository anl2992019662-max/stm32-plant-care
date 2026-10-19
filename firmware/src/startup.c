/* Cortex-M3 vectors and reset entry for Arm Compiler 6 + microlib. */
#include <stdint.h>
#include "board.h"

__attribute__((section(".bss.STACK"),aligned(8),used)) uint64_t plant_stack[512];
__asm(".global __initial_sp\n.set __initial_sp, plant_stack + 4096\n.size __initial_sp, 0\n"
      ".global __heap_base\n.set __heap_base, plant_stack\n.size __heap_base, 0\n"
      ".global __heap_limit\n.set __heap_limit, plant_stack\n.size __heap_limit, 0\n");
extern uint32_t __initial_sp;
extern void SystemInit(void);
extern void SysTick_Handler(void);
extern void USART1_IRQHandler(void);
extern void USART2_IRQHandler(void);
extern void HardFault_Handler(void);
extern void MemManage_Handler(void);
extern void BusFault_Handler(void);
extern void UsageFault_Handler(void);

__attribute__((noreturn)) static void unexpected(void) { for (;;) { __WFI(); } }
__attribute__((naked,noreturn)) void Reset_Handler(void) {
    __asm volatile("bl SystemInit\n b __main\n");
}
typedef void (*handler_t)(void);
#define D unexpected
__attribute__((section("RESET"),used,aligned(256))) const handler_t vectors[]={
    (handler_t)&__initial_sp,Reset_Handler,D,HardFault_Handler,MemManage_Handler,BusFault_Handler,UsageFault_Handler,
    0,0,0,0,D,D,0,D,SysTick_Handler,
    /* IRQ 0..10 */ D,D,D,D,D,D,D,D,D,D,D,
    /* IRQ 11..20 */ D,D,D,D,D,D,D,D,D,D,
    /* IRQ 21..30 */ D,D,D,D,D,D,D,D,D,D,
    /* IRQ 31..40 */ D,D,D,D,D,D,USART1_IRQHandler,USART2_IRQHandler,D,D,
    /* IRQ 41..42 */ D,D
};
#undef D
