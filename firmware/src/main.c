#include "board.h"
#include <stdio.h>
#include <string.h>

static plant_t plant;
static plant_sensors_t sensors;
static line_reader_t lines;
static char message[384];
static uint32_t app_errors;
static bool display_ok,settings_page;
static uint8_t selected_device,selected_setting;
static const char *const names[]={"TEMP_ON","TEMP_OFF","SOIL_ON","SOIL_OFF","LIGHT_ON","LIGHT_OFF",
    "SHADE_ON","SHADE_OFF","CO2_ON","CO2_OFF","PUMP_MS","COOLDOWN_MS"};
static const uint32_t increments[]={5,5,5,5,5,5,5,5,100,100,500,10000};

static void sample_adc(uint32_t now) {
    static uint16_t soil[5],light[5]; static uint8_t index,count;
    uint16_t a,b;
    if (!board_adc(0,&a) || !board_adc(1,&b)) { sensors.valid &= (uint8_t)~(VALID_SOIL|VALID_LIGHT); return; }
    soil[index]=a; light[index]=b; index=(uint8_t)((index+1)%5); if (count<5) ++count;
    sensors.soil_raw=a; sensors.light_raw=b;
    if (count<5) return;
    sensors.soil_raw=median5(soil); sensors.light_raw=median5(light);
    /* Rail checks use the current sample too, so a disconnection stops watering promptly. */
    if (a>=20 && a<=4075 && adc_percent(sensors.soil_raw,plant.cfg.soil_dry,plant.cfg.soil_wet,&sensors.soil_pct)) {
        sensors.valid |= VALID_SOIL; sensors.soil_at=now;
    } else sensors.valid &= (uint8_t)~VALID_SOIL;
    if (b>=20 && b<=4075 && adc_percent(sensors.light_raw,plant.cfg.light_dark,plant.cfg.light_bright,&sensors.light_pct)) {
        sensors.valid |= VALID_LIGHT; sensors.light_at=now;
    } else sensors.valid &= (uint8_t)~VALID_LIGHT;
}
static void sample_temperature(uint32_t now) {
    static bool converting; static uint32_t at,retry;
    if (!converting) {
        if (now-retry<1500) return;
        retry=now;
        if (board_temperature_start()) { converting=true; at=now; }
        else sensors.valid &= (uint8_t)~VALID_TEMP;
    } else if (now-at>=800) {
        if (board_temperature_read(&sensors.temp_dC)) { sensors.valid|=VALID_TEMP; sensors.temp_at=board_millis(); }
        else sensors.valid &= (uint8_t)~VALID_TEMP;
        converting=false;
    }
}
static void sample_co2(uint32_t now) {
    static uint8_t frame[9],used; static uint32_t last_query; static bool awaiting;
    uint8_t b;
    if (now-last_query>=5000) {
        while (board_co2_read(&b)) { }
        used=0; awaiting=board_co2_query(); last_query=now;
    }
    if (awaiting && now-last_query>1000) { awaiting=false; sensors.valid &= (uint8_t)~VALID_CO2; used=0; }
    while (board_co2_read(&b)) {
        if (!awaiting) continue;
        if (used==0 && b!=0xff) continue;
        frame[used++]=b;
        if (used<9) continue;
        uint16_t ppm;
        if (mhz_decode(frame,&ppm)) {
            sensors.co2_ppm=ppm; sensors.co2_at=now; sensors.valid|=VALID_CO2; awaiting=false; used=0;
        } else {
            ++app_errors;
            memmove(frame,frame+1,8); used=8;
            while (used && frame[0]!=0xff) { memmove(frame,frame+1,--used); }
        }
    }
}
static void telemetry(void) {
    plant_telemetry(&plant,&sensors,board_millis(),board_rtc_get(),board_io_errors()+app_errors,message,sizeof(message));
    (void)board_bt_write(message);
}
static void save(void) {
    if (plant.mode!=MODE_MANUAL || plant.outputs || plant.request) { (void)board_bt_write("ERR IDLE_REQUIRED\n"); return; }
    (void)board_bt_write(board_config_save(&plant.cfg)?"OK SAVED\n":"ERR FLASH\n");
}
static void serial_commands(uint32_t now) {
    static uint32_t last_errors;
    uint32_t errors=board_io_errors();
    if (errors!=last_errors) { lines.discard=true; last_errors=errors; }
    uint8_t byte;
    unsigned limit=128;
    while (limit-- && board_bt_read(&byte)) {
        int result=line_feed(&lines,(char)byte);
        if (result<0) { (void)board_bt_write("ERR LINE_DISCARDED\n"); continue; }
        if (!result) continue;
        if (!strcmp(lines.text,"GET")) telemetry();
        else if (!strcmp(lines.text,"SAVE")) save();
        else if (!strncmp(lines.text,"TIME ",5)) {
            uint32_t epoch;
            bool ok=plant.mode==MODE_MANUAL && !plant.outputs && !plant.request &&
                parse_u32(lines.text+5,&epoch) && board_rtc_set(epoch);
            (void)board_bt_write(ok?"OK TIME\n":"ERR TIME_OR_IDLE\n");
        } else {
            plant_command(&plant,lines.text,now,message,sizeof(message));
            (void)board_bt_write(message);
        }
    }
}
static void key_action(unsigned key,bool long_press,uint32_t now) {
    if (key==0) {
        settings_page=false;
        if (long_press) plant_stop(&plant,now);
        else {
            plant_mode_t next=(plant_mode_t)((plant.mode+1)%3);
            if (next==MODE_AUTO && plant.cfg.cal_flags!=3) next=MODE_REMOTE;
            (void)plant_mode(&plant,next,now);
        }
    } else if (key==1 && long_press) {
        plant_stop(&plant,now); settings_page=!settings_page;
    } else if (key==3 && long_press) save();
    else if (!long_press && key==1) {
        if (settings_page) selected_setting=(uint8_t)((selected_setting+1)%12);
        else selected_device=(uint8_t)((selected_device+1)%4);
    } else if (!long_press && (key==2||key==3)) {
        if (settings_page && plant.mode==MODE_MANUAL && !plant.outputs && !plant.request) {
            uint32_t values[17]; memcpy(values,&plant.cfg,sizeof(values));
            uint32_t old=values[selected_setting],step=increments[selected_setting];
            if (key==2 && old<=UINT32_MAX-step) (void)plant_threshold(&plant.cfg,names[selected_setting],old+step);
            if (key==3 && old>=step) (void)plant_threshold(&plant.cfg,names[selected_setting],old-step);
        } else if (plant.mode==MODE_MANUAL) {
            (void)plant_manual(&plant,(uint8_t)(1u<<selected_device),key==2,now);
        }
    }
}
static void keys_task(uint32_t now) {
    static uint8_t raw,stable; static uint32_t changed,pressed[4]; static bool fired[4];
    uint8_t value=board_keys();
    if (value!=raw) { raw=value; changed=now; }
    if (now-changed>=30 && raw!=stable) {
        uint8_t old=stable; stable=raw;
        for (unsigned i=0;i<4;++i) {
            uint8_t mask=(uint8_t)(1u<<i);
            if ((stable&mask) && !(old&mask)) { pressed[i]=now; fired[i]=false; }
            if (!(stable&mask) && (old&mask) && !fired[i]) key_action(i,false,now);
        }
    }
    for (unsigned i=0;i<4;++i) if ((stable&(1u<<i)) && !fired[i] && now-pressed[i]>=1200) {
        fired[i]=true; key_action(i,true,now);
    }
}
static void display_task(uint32_t now) {
    char line[40]; oled_clear();
    const char *modes[]={"MANUAL","AUTO","REMOTE"};
    uint32_t epoch=board_rtc_get();
    if (epoch) snprintf(line,sizeof(line),"%s %02lu:%02lu:%02lu UTC",modes[plant.mode],
        (unsigned long)((epoch/3600u)%24u),(unsigned long)((epoch/60u)%60u),(unsigned long)(epoch%60u));
    else snprintf(line,sizeof(line),"PLANT %s %lus",modes[plant.mode],(unsigned long)(now/1000));
    oled_text(0,line);
    if (settings_page) {
        uint32_t values[17]; memcpy(values,&plant.cfg,sizeof(values));
        oled_text(1,"SETTINGS: RAM UNTIL SAVE"); oled_text(2,names[selected_setting]);
        snprintf(line,sizeof(line),"VALUE %lu",(unsigned long)values[selected_setting]); oled_text(3,line);
        oled_text(4,"K2 NEXT K3 + K4 -"); oled_text(5,"LONG K4 SAVE WHEN IDLE");
        oled_text(6,"LONG K2 EXIT / STOP");
    } else {
        int absolute=sensors.temp_dC<0?-sensors.temp_dC:sensors.temp_dC;
        if (plant.valid&VALID_TEMP) snprintf(line,sizeof(line),"TEMP %s%d.%d C",sensors.temp_dC<0?"-":"",absolute/10,absolute%10);
        else snprintf(line,sizeof(line),"TEMP -- C"); oled_text(1,line);
        if ((plant.valid&VALID_SOIL) && (plant.cfg.cal_flags&CAL_SOIL)) snprintf(line,sizeof(line),"SOIL %u%% RAW %u",sensors.soil_pct,sensors.soil_raw);
        else snprintf(line,sizeof(line),"SOIL -- RAW %u",sensors.soil_raw); oled_text(2,line);
        if ((plant.valid&VALID_LIGHT) && (plant.cfg.cal_flags&CAL_LIGHT)) snprintf(line,sizeof(line),"LIGHT %u%% RAW %u",sensors.light_pct,sensors.light_raw);
        else snprintf(line,sizeof(line),"LIGHT -- RAW %u",sensors.light_raw); oled_text(3,line);
        if (plant.valid&VALID_CO2) snprintf(line,sizeof(line),"CO2 %u PPM",sensors.co2_ppm);
        else snprintf(line,sizeof(line),"CO2 %s",now<CO2_WARMUP_MS?"WARMUP":"FAULT"); oled_text(4,line);
        snprintf(line,sizeof(line),"OUT F%u P%u L%u S%u",!!(plant.outputs&1),!!(plant.outputs&2),!!(plant.outputs&4),!!(plant.outputs&8)); oled_text(5,line);
        const char *devices[]={"FAN","PUMP","LAMP","SHADE"};
        snprintf(line,sizeof(line),"SELECT %s WATER %s",devices[selected_device],sensors.water_ok?"OK":"LOW"); oled_text(6,line);
    }
    snprintf(line,sizeof(line),"FAULT %lu CAL %lu RTC %s",(unsigned long)plant.faults,(unsigned long)plant.cfg.cal_flags,board_rtc_precise()?"LSE":"LSI"); oled_text(7,line);
}
int main(void) {
    board_init(); board_apply(0,false);
    plant_config_t config; (void)board_config_load(&config); plant_init(&plant,&config,board_millis());
    board_rtc_init(); display_ok=oled_init();
    (void)board_bt_write("BOOT PLANT " PLANT_VERSION " MANUAL OUTPUTS_OFF\n");
    uint32_t adc_at=0,screen_at=0,page_at=0,telemetry_at=0; uint8_t page=0;
    bool co2_alarm=false;
    for (;;) {
        uint32_t now=board_millis(); board_watchdog();
        if (now-adc_at>=100) { adc_at=now; sample_adc(now); }
        sample_temperature(now); sample_co2(now); keys_task(now); serial_commands(now);
        sensors.water_ok=board_water_ok(); now=board_millis();
        plant_step(&plant,&sensors,now);
        if (!(plant.valid&VALID_CO2)) co2_alarm=false;
        else if (sensors.co2_ppm>=plant.cfg.co2_on) co2_alarm=true;
        else if (sensors.co2_ppm<=plant.cfg.co2_off) co2_alarm=false;
        uint32_t alarms=plant.faults;
        if (now-plant.boot_at<CO2_WARMUP_MS) alarms &= ~FAULT_CO2;
        bool beep=(co2_alarm && now%1000<200) || (alarms && now%3000<80);
        board_apply(plant.outputs,beep);
        if (now-telemetry_at>=2000) { telemetry_at=now; telemetry(); }
        if (now-screen_at>=500) { screen_at=now; display_task(now); }
        if (display_ok && now-page_at>=60) {
            page_at=now;
            if (!oled_flush_page(page)) { display_ok=false; ++app_errors; }
            page=(uint8_t)((page+1)%8);
        }
        if (!display_ok && now-page_at>=5000) { page_at=now; display_ok=oled_init(); }
        __WFI();
    }
}
