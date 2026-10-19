/* Software-only adapter around the actual firmware control core. */
#include "plant.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static bool sensor_values(const char *text,long values[5]) {
    for (unsigned i=0;i<5;++i) {
        while (*text==' ') ++text;
        char *end; errno=0;
        values[i]=strtol(text,&end,10);
        if (text==end || errno==ERANGE || (*end && *end!=' ')) return false;
        text=end;
    }
    while (*text==' ') ++text;
    return !*text;
}

int main(void) {
    plant_t p; plant_config_t cfg; plant_defaults(&cfg); cfg.cal_flags=3;
    plant_init(&p,&cfg,0);
    uint32_t now=CO2_WARMUP_MS;
    plant_sensors_t s={0}; s.valid=15; s.water_ok=true;
    s.temp_dC=250; s.soil_pct=60; s.light_pct=50; s.co2_ppm=700;
    char reply[384]; line_reader_t line={0}; int ch;
    puts("DEMO SOFTWARE ONLY: SAME C CONTROL CORE; NO HARDWARE"); fflush(stdout);
    while ((ch=getchar())!=EOF) {
        int result=line_feed(&line,(char)ch);
        if (!result) continue;
        if (result<0) { puts("ERR LINE"); fflush(stdout); continue; }
        bool special=false;
        if (!strncmp(line.text,"TICK ",5)) {
            uint32_t ms;
            if (parse_u32(line.text+5,&ms) && ms<=600000) { now+=ms; special=true; }
            else { puts("ERR TICK"); fflush(stdout); continue; }
        } else if (!strncmp(line.text,"SENS ",5)) {
            long values[5];
            if (sensor_values(line.text+5,values) &&
                values[0]>=-550 && values[0]<=1250 && values[1]>=0 && values[1]<=100 && values[2]>=0 && values[2]<=100 &&
                values[3]>=400 && values[3]<=10000 && (values[4]==0||values[4]==1)) {
                s.temp_dC=(int16_t)values[0]; s.soil_pct=(uint16_t)values[1]; s.light_pct=(uint16_t)values[2];
                s.co2_ppm=(uint16_t)values[3]; s.water_ok=values[4]!=0; special=true;
            } else { puts("ERR SENS"); fflush(stdout); continue; }
        }
        s.temp_at=s.soil_at=s.light_at=s.co2_at=now;
        s.soil_raw=(uint16_t)(3000-s.soil_pct*15); s.light_raw=(uint16_t)(3500-s.light_pct*30);
        if (!special && strcmp(line.text,"GET")) {
            plant_command(&p,line.text,now,reply,sizeof(reply)); fputs(reply,stdout);
        }
        plant_step(&p,&s,now);
        if (special || !strcmp(line.text,"GET")) {
            plant_telemetry(&p,&s,now,0,0,reply,sizeof(reply)); fputs(reply,stdout);
        }
        fflush(stdout);
    }
    return 0;
}
