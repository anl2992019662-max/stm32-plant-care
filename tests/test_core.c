#include "plant.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Tests require enabled assertions
#endif

static unsigned groups;
#define PASS() do { ++groups; printf("PASS %s\n",__func__); } while(0)
static plant_sensors_t healthy(uint32_t now) {
    plant_sensors_t s={0}; s.valid=15; s.water_ok=true;
    s.temp_dC=250; s.soil_pct=60; s.light_pct=50; s.co2_ppm=700;
    s.temp_at=s.soil_at=s.light_at=s.co2_at=now; s.soil_raw=2000; s.light_raw=1500;
    return s;
}
static void step(plant_t *p,plant_sensors_t *s,uint32_t now) {
    s->temp_at=s->soil_at=s->light_at=s->co2_at=now; plant_step(p,s,now);
}
static void ready(plant_t *p) {
    plant_config_t c; plant_defaults(&c); c.cal_flags=3; plant_init(p,&c,0);
}
static void test_boot_and_calibration(void) {
    plant_t p; plant_init(&p,NULL,0); assert(p.mode==MODE_MANUAL && !p.outputs);
    assert(!plant_mode(&p,MODE_AUTO,0));
    assert(!plant_mode(&p,(plant_mode_t)-1,0));
    char reply[384]; plant_command(&p,"CAL SOIL 3000 1500",1,reply,sizeof(reply)); assert(strstr(reply,"OK CAL"));
    assert(!plant_mode(&p,MODE_AUTO,1));
    plant_command(&p,"CAL LIGHT 3500 500",2,reply,sizeof(reply)); assert(strstr(reply,"OK CAL"));
    assert(plant_mode(&p,MODE_AUTO,2));
    plant_sensors_t s=healthy(2); s.soil_pct=10; s.water_ok=false; step(&p,&s,2); assert(!(p.outputs&OUT_PUMP));
    PASS();
}
static void test_hysteresis_and_stale(void) {
    plant_t p; ready(&p); assert(plant_mode(&p,MODE_AUTO,CO2_WARMUP_MS));
    plant_sensors_t s=healthy(CO2_WARMUP_MS); s.temp_dC=301; s.light_pct=20;
    step(&p,&s,CO2_WARMUP_MS); assert(p.outputs==(OUT_FAN|OUT_LAMP));
    s.temp_dC=290; s.light_pct=30; step(&p,&s,CO2_WARMUP_MS+20); assert(p.outputs==(OUT_FAN|OUT_LAMP));
    s.temp_dC=279; s.light_pct=85; step(&p,&s,CO2_WARMUP_MS+40); assert(p.outputs==OUT_SHADE);
    s.co2_ppm=1600; step(&p,&s,CO2_WARMUP_MS+60); assert(p.outputs==(OUT_SHADE|OUT_FAN));
    s.co2_ppm=1300; step(&p,&s,CO2_WARMUP_MS+80); assert(p.outputs&OUT_FAN);
    s.co2_ppm=1100; step(&p,&s,CO2_WARMUP_MS+100); assert(!(p.outputs&OUT_FAN));
    plant_step(&p,&s,CO2_WARMUP_MS+13000); assert(p.outputs==0 && (p.faults&15)==15);
    PASS();
}
static void test_pump_limits_and_repetition(void) {
    plant_t p; ready(&p); assert(plant_mode(&p,MODE_REMOTE,100));
    plant_sensors_t s=healthy(100); assert(plant_manual(&p,OUT_PUMP,true,100)); step(&p,&s,100);
    assert(p.outputs&OUT_PUMP);
    for (uint32_t t=200;t<=3100;t+=100) {
        assert(plant_manual(&p,OUT_PUMP,true,t)); step(&p,&s,t);
    }
    assert(!(p.outputs&OUT_PUMP) && p.pump_used_ms==3000);
    assert(plant_manual(&p,OUT_PUMP,true,3200)); step(&p,&s,3200); assert(!(p.outputs&OUT_PUMP));
    assert(plant_mode(&p,MODE_REMOTE,3300)); assert(plant_manual(&p,OUT_PUMP,true,3300)); step(&p,&s,3300);
    assert(!(p.outputs&OUT_PUMP)); /* changing modes cannot bypass cooldown */
    assert(plant_mode(&p,MODE_REMOTE,63100)); assert(plant_manual(&p,OUT_PUMP,true,63100)); step(&p,&s,63100);
    assert(p.outputs&OUT_PUMP);
    s.water_ok=false; step(&p,&s,63200); assert(!(p.outputs&OUT_PUMP) && (p.faults&FAULT_WATER));
    PASS();
}
static void test_budget_and_stop_accounting(void) {
    plant_t p; ready(&p); plant_sensors_t s=healthy(100);
    for (unsigned pulse=0;pulse<6;++pulse) {
        uint32_t now=100+pulse*70000u;
        assert(plant_mode(&p,MODE_REMOTE,now)); assert(plant_manual(&p,OUT_PUMP,true,now)); step(&p,&s,now);
        if (pulse<5) {
            assert(p.outputs&OUT_PUMP); plant_stop(&p,now+3000); assert(p.pump_used_ms==(pulse+1)*3000u);
        } else assert(!(p.outputs&OUT_PUMP) && (p.faults&FAULT_BUDGET));
    }
    step(&p,&s,BUDGET_WINDOW_MS+100); assert(p.pump_used_ms==0);
    PASS();
}
static void test_leases_and_wrap(void) {
    plant_t p; ready(&p); plant_sensors_t s=healthy(100);
    assert(plant_mode(&p,MODE_REMOTE,100)); assert(plant_manual(&p,OUT_FAN,true,100)); step(&p,&s,100);
    step(&p,&s,10100); assert(p.mode==MODE_MANUAL && p.outputs==0 && (p.faults&FAULT_REMOTE));
    assert(plant_manual(&p,OUT_LAMP,true,10200)); step(&p,&s,10200); assert(p.outputs&OUT_LAMP);
    step(&p,&s,40200); assert(!p.outputs);
    uint32_t near=UINT32_MAX-5000; plant_init(&p,NULL,near); assert(plant_manual(&p,OUT_FAN,true,near));
    s=healthy(near); step(&p,&s,near); step(&p,&s,near+29999u); assert(p.outputs&OUT_FAN);
    step(&p,&s,near+30000u); assert(!p.outputs);
    PASS();
}
static void test_protocol_rejects_bad_inputs(void) {
    plant_t p; ready(&p); char reply[384];
    const char *bad[]={"SET PUMP 1","MODE AUTO EXTRA","THRESH TEMP_ON 4294967296","THRESH SOIL_ON 4294967294",
        "THRESH LIGHT_ON 4294967294","THRESH CO2_OFF 4294967294","THRESH TEMP_ON -1","CAL SOIL 2000 2000","CAL SOIL 0 4095"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        plant_command(&p,bad[i],1,reply,sizeof(reply)); assert(!strncmp(reply,"ERR",3));
    }
    assert(plant_mode(&p,MODE_REMOTE,2)); plant_command(&p,"SET SHADE 1",3,reply,sizeof(reply)); assert(strstr(reply,"OK REQUEST"));
    plant_command(&p,"SET LAMP 1",4,reply,sizeof(reply)); plant_sensors_t s=healthy(4); step(&p,&s,4);
    assert(p.outputs==OUT_SHADE); plant_command(&p,"STOP",5,reply,sizeof(reply)); assert(!p.outputs && !p.request);
    plant_command(&p,"THRESH TEMP_ON 320",6,reply,sizeof(reply)); assert(p.cfg.temp_on==320);
    PASS();
}
static void test_line_overflow_and_nonascii(void) {
    line_reader_t r={0};
    for (unsigned i=0;i<150;++i) assert(line_feed(&r,'A')==0);
    assert(line_feed(&r,'\n')==-1);
    const char *good="STOP\r\n"; int result=0;
    for (;*good;++good) result=line_feed(&r,*good);
    assert(result==1 && !strcmp(r.text,"STOP"));
    line_feed(&r,'S'); line_feed(&r,'\0'); assert(line_feed(&r,'\n')==-1);
    PASS();
}
static void test_adc_and_sensor_integrity(void) {
    uint16_t pct; assert(adc_percent(2250,3000,1500,&pct) && pct==50);
    assert(adc_percent(2250,1500,3000,&pct) && pct==50);
    assert(adc_percent(3500,3000,1500,&pct) && pct==0);
    assert(!adc_percent(4095,3000,1500,&pct));
    uint16_t readings[]={2001,0,2000,4095,1999}; assert(median5(readings)==2000);
    uint8_t scratch[]={0x91,0x01,0x4b,0x46,0x7f,0xff,0x0c,0x10,0x70};
    int16_t temp; assert(ds18_decode(scratch,&temp) && temp==250);
    scratch[0]^=1; assert(!ds18_decode(scratch,&temp));
    scratch[0]=0x5e; scratch[1]=0xff; scratch[8]=crc8_dallas(scratch,8); assert(ds18_decode(scratch,&temp) && temp==-101);
    uint8_t frame[]={0xff,0x86,0x01,0xf4,0,0,0,0,0x85}; uint16_t ppm;
    assert(mhz_decode(frame,&ppm) && ppm==500); frame[8]^=1; assert(!mhz_decode(frame,&ppm));
    assert(crc32_ieee((const uint8_t *)"123456789",9)==0xcbf43926u);
    PASS();
}
static void test_storage_crc_and_torn_commit(void) {
    plant_config_t c,read; plant_defaults(&c); c.cal_flags=3; uint8_t record[RECORD_BYTES]; uint32_t seq;
    config_pack(record,&c,UINT32_MAX); assert(config_unpack(record,&read,&seq) && seq==UINT32_MAX);
    assert(!memcmp(&c,&read,sizeof(c)));
    for (unsigned i=0;i<RECORD_BYTES;++i) {
        record[i]^=1; assert(!config_unpack(record,&read,&seq)); record[i]^=1;
    }
    record[0]=0xff; record[1]=0xff; assert(!config_unpack(record,&read,&seq));
    PASS();
}
static void test_telemetry_warmup_nulls(void) {
    plant_t p; ready(&p); plant_sensors_t s=healthy(100); step(&p,&s,100);
    char msg[384]; plant_telemetry(&p,&s,100,0,0,msg,sizeof(msg));
    assert(strstr(msg,"\"co2\":null") && strstr(msg,"\"t_dC\":250"));
    assert(strlen(msg)<sizeof(msg)-1 && msg[strlen(msg)-1]=='\n');
    s.valid=0; step(&p,&s,CO2_WARMUP_MS); plant_telemetry(&p,&s,CO2_WARMUP_MS,0,0,msg,sizeof(msg));
    assert(strstr(msg,"\"soil\":null") && strstr(msg,"\"light\":null") && strstr(msg,"\"t_dC\":null"));
    PASS();
}
static void test_randomized_control_invariants(void) {
    plant_t p; ready(&p); uint32_t random=0x5a17c9u,now=CO2_WARMUP_MS;
    plant_sensors_t s=healthy(now);
    for (unsigned i=0;i<50000;++i) {
        random=random*1664525u+1013904223u; now+=random%701u;
        s.temp_dC=(int16_t)((random>>8)%601); s.soil_pct=(uint16_t)((random>>12)%101);
        s.light_pct=(uint16_t)((random>>16)%101); s.co2_ppm=(uint16_t)(400+(random>>20)%3000);
        s.water_ok=(random&16u)!=0; s.valid=(uint8_t)((random>>4)&15u);
        if (i%13==0) (void)plant_mode(&p,(plant_mode_t)(random%3u),now);
        if (p.mode!=MODE_AUTO) (void)plant_manual(&p,(uint8_t)(1u<<(random%4u)),(random&8u)!=0,now);
        step(&p,&s,now);
        assert((p.outputs&~15u)==0);
        assert((p.outputs&(OUT_LAMP|OUT_SHADE))!=(OUT_LAMP|OUT_SHADE));
        assert(p.pump_used_ms<=PUMP_BUDGET_MS);
        if (p.outputs&OUT_PUMP) {
            assert(s.water_ok && (p.valid&VALID_SOIL) && (p.cfg.cal_flags&CAL_SOIL));
            assert(now-p.pump_since<p.cfg.pump_max_ms && p.pump_used_ms<PUMP_BUDGET_MS);
        }
    }
    PASS();
}
int main(void) {
    test_boot_and_calibration(); test_hysteresis_and_stale(); test_pump_limits_and_repetition();
    test_budget_and_stop_accounting(); test_leases_and_wrap(); test_protocol_rejects_bad_inputs();
    test_line_overflow_and_nonascii(); test_adc_and_sensor_integrity(); test_storage_crc_and_torn_commit(); test_telemetry_warmup_nulls();
    test_randomized_control_invariants();
    printf("%u test groups passed. No hardware claims.\n",groups); return 0;
}
