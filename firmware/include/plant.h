#ifndef PLANT_H
#define PLANT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLANT_VERSION "1.0.0"
#define CAL_SOIL 1u
#define CAL_LIGHT 2u
#define VALID_TEMP 1u
#define VALID_SOIL 2u
#define VALID_LIGHT 4u
#define VALID_CO2 8u
#define OUT_FAN 1u
#define OUT_PUMP 2u
#define OUT_LAMP 4u
#define OUT_SHADE 8u
#define FAULT_TEMP 1u
#define FAULT_SOIL 2u
#define FAULT_LIGHT 4u
#define FAULT_CO2 8u
#define FAULT_WATER 16u
#define FAULT_REMOTE 32u
#define FAULT_BUDGET 64u
#define FAULT_CAL 128u
#define CO2_WARMUP_MS 180000u
#define REMOTE_LEASE_MS 10000u
#define MANUAL_LEASE_MS 30000u
#define PUMP_BUDGET_MS 15000u
#define BUDGET_WINDOW_MS 3600000u

typedef enum { MODE_MANUAL, MODE_AUTO, MODE_REMOTE } plant_mode_t;
/* All fields are uint32_t: persisted format is independent of bool padding. */
typedef struct {
    uint32_t temp_on, temp_off, soil_on, soil_off;
    uint32_t light_on, light_off, shade_on, shade_off;
    uint32_t co2_on, co2_off, pump_max_ms, cooldown_ms;
    uint32_t soil_dry, soil_wet, light_dark, light_bright, cal_flags;
} plant_config_t;

typedef struct {
    int16_t temp_dC;
    uint16_t soil_pct, light_pct, co2_ppm, soil_raw, light_raw;
    uint8_t valid;
    bool water_ok;
    uint32_t temp_at, soil_at, light_at, co2_at;
} plant_sensors_t;

typedef struct {
    plant_config_t cfg;
    plant_mode_t mode;
    uint8_t request, outputs, valid;
    uint32_t faults, boot_at, last_step, activity_at;
    uint32_t pump_since, pump_stop, budget_start, pump_used_ms;
    bool has_pumped, temp_fan, co2_fan, lamp, shade, auto_pump;
    bool remote_expired;
} plant_t;

typedef struct { char text[96]; size_t used; bool discard; } line_reader_t;

void plant_defaults(plant_config_t *cfg);
bool plant_config_valid(const plant_config_t *cfg);
void plant_init(plant_t *p, const plant_config_t *cfg, uint32_t now);
bool plant_mode(plant_t *p, plant_mode_t mode, uint32_t now);
void plant_stop(plant_t *p, uint32_t now);
void plant_step(plant_t *p, const plant_sensors_t *s, uint32_t now);
bool plant_manual(plant_t *p, uint8_t mask, bool on, uint32_t now);
bool plant_threshold(plant_config_t *cfg, const char *name, uint32_t value);
void plant_command(plant_t *p, const char *line, uint32_t now, char *reply, size_t capacity);
void plant_telemetry(const plant_t *p, const plant_sensors_t *s, uint32_t now,
                     uint32_t epoch, uint32_t io_errors, char *out, size_t capacity);
int line_feed(line_reader_t *r, char ch);
bool parse_u32(const char *text, uint32_t *value);
bool adc_percent(uint16_t raw, uint32_t zero, uint32_t hundred, uint16_t *percent);
uint16_t median5(const uint16_t values[5]);
uint8_t crc8_dallas(const uint8_t *data, size_t length);
uint32_t crc32_ieee(const uint8_t *data, size_t length);
bool ds18_decode(const uint8_t scratch[9], int16_t *temp_dC);
bool mhz_decode(const uint8_t frame[9], uint16_t *ppm);
#define RECORD_BYTES 84u
void config_pack(uint8_t record[RECORD_BYTES], const plant_config_t *cfg, uint32_t seq);
bool config_unpack(const uint8_t record[RECORD_BYTES], plant_config_t *cfg, uint32_t *seq);
#endif
