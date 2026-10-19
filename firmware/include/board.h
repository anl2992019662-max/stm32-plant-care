#ifndef BOARD_H
#define BOARD_H
#include "plant.h"
#include "stm32f1xx.h"

#define BOARD_CLOCK_HZ 64000000u
#define SERVO_OPEN_US 1000u
#define SERVO_SHADE_US 1500u
/* Physically test linkage without a load before increasing travel. */
void board_init(void);
uint32_t board_millis(void);
void board_delay_us(uint32_t us);
void board_watchdog(void);
bool board_adc(uint8_t channel, uint16_t *value);
void board_apply(uint8_t outputs, bool alarm);
bool board_water_ok(void);
uint8_t board_keys(void);
bool board_bt_write(const char *text);
bool board_bt_read(uint8_t *byte);
bool board_co2_read(uint8_t *byte);
bool board_co2_query(void);
uint32_t board_io_errors(void);
bool board_temperature_start(void);
bool board_temperature_read(int16_t *temp_dC);
void board_rtc_init(void);
uint32_t board_rtc_get(void);
bool board_rtc_set(uint32_t epoch);
bool board_rtc_precise(void);
bool board_config_load(plant_config_t *cfg);
bool board_config_save(const plant_config_t *cfg);
bool oled_init(void);
bool oled_flush_page(uint8_t page);
void oled_clear(void);
void oled_text(uint8_t row, const char *text);
#endif
