#include "plant.h"
#include <stdio.h>
#include <string.h>

static bool elapsed(uint32_t now, uint32_t then, uint32_t ms) { return now - then >= ms; }
void plant_defaults(plant_config_t *c) {
    const plant_config_t defaults = {
        300,280,35,45,25,40,80,65,1500,1200,3000,60000,3000,1500,3500,500,0
    };
    *c = defaults;
}
static uint32_t distance(uint32_t a, uint32_t b) { return a > b ? a-b : b-a; }
bool plant_config_valid(const plant_config_t *c) {
    return c->temp_on <= 600 && c->temp_on >= 50 && c->temp_off < c->temp_on &&
        c->temp_on - c->temp_off >= 5 &&
        c->soil_on >= 5 && c->soil_on <= 90 && c->soil_off <= 95 && c->soil_off >= c->soil_on+5 &&
        c->light_on >= 1 && c->light_on <= 80 && c->light_off <= 90 && c->light_off >= c->light_on+5 &&
        c->shade_off <= 94 && c->shade_off >= c->light_off+5 && c->shade_on >= c->shade_off+5 && c->shade_on <= 99 &&
        c->co2_off >= 400 && c->co2_off <= 4900 && c->co2_on >= c->co2_off+100 && c->co2_on <= 5000 &&
        c->pump_max_ms >= 500 && c->pump_max_ms <= 5000 &&
        c->cooldown_ms >= 30000 && c->cooldown_ms <= 600000 &&
        c->soil_dry >= 100 && c->soil_dry <= 4000 && c->soil_wet >= 100 && c->soil_wet <= 4000 &&
        c->light_dark >= 100 && c->light_dark <= 4000 && c->light_bright >= 100 && c->light_bright <= 4000 &&
        distance(c->soil_dry,c->soil_wet) >= 200 && distance(c->light_dark,c->light_bright) >= 200 &&
        (c->cal_flags & ~3u) == 0;
}
void plant_init(plant_t *p, const plant_config_t *cfg, uint32_t now) {
    memset(p,0,sizeof(*p));
    if (cfg && plant_config_valid(cfg)) p->cfg = *cfg; else plant_defaults(&p->cfg);
    p->boot_at = p->last_step = p->budget_start = p->activity_at = now;
    p->mode = MODE_MANUAL;
}
static void account_pump(plant_t *p, uint32_t now) {
    uint32_t dt=now-p->last_step;
    p->last_step=now;
    if (p->outputs & OUT_PUMP) {
        uint32_t remaining=PUMP_BUDGET_MS-p->pump_used_ms;
        p->pump_used_ms += dt > remaining ? remaining : dt;
    }
}
static void all_off(plant_t *p, uint32_t now) {
    account_pump(p,now);
    if (p->outputs & OUT_PUMP) { p->pump_stop=now; p->has_pumped=true; }
    p->outputs=p->request=0;
    p->temp_fan=p->co2_fan=p->lamp=p->shade=p->auto_pump=false;
}
bool plant_mode(plant_t *p, plant_mode_t mode, uint32_t now) {
    if ((unsigned)mode > (unsigned)MODE_REMOTE) return false;
    if (mode == MODE_AUTO && p->cfg.cal_flags != (CAL_SOIL|CAL_LIGHT)) return false;
    all_off(p,now);
    p->mode=mode; p->activity_at=now; p->remote_expired=false;
    return true;
}
void plant_stop(plant_t *p, uint32_t now) { (void)plant_mode(p,MODE_MANUAL,now); }
bool plant_manual(plant_t *p, uint8_t mask, bool on, uint32_t now) {
    if (p->mode == MODE_AUTO || !mask || (mask & ~15u)) return false;
    if (on) p->request |= mask; else p->request &= (uint8_t)~mask;
    p->activity_at=now;
    return true;
}
void plant_step(plant_t *p, const plant_sensors_t *s, uint32_t now) {
    /* Conservative accounting: a delayed loop charges all elapsed ON time. */
    account_pump(p,now);
    if (elapsed(now,p->budget_start,BUDGET_WINDOW_MS)) {
        p->budget_start=now; p->pump_used_ms=0;
    }
    p->valid=s->valid;
    if (elapsed(now,s->temp_at,3000)) p->valid &= (uint8_t)~VALID_TEMP;
    if (elapsed(now,s->soil_at,1500)) p->valid &= (uint8_t)~VALID_SOIL;
    if (elapsed(now,s->light_at,1500)) p->valid &= (uint8_t)~VALID_LIGHT;
    if (!elapsed(now,p->boot_at,CO2_WARMUP_MS) || elapsed(now,s->co2_at,12000))
        p->valid &= (uint8_t)~VALID_CO2;
    p->faults=(uint32_t)(~p->valid)&15u;
    if (!s->water_ok) p->faults |= FAULT_WATER;
    if (p->cfg.cal_flags != 3u) p->faults |= FAULT_CAL;
    if (p->pump_used_ms >= PUMP_BUDGET_MS) p->faults |= FAULT_BUDGET;
    if (p->mode == MODE_REMOTE && elapsed(now,p->activity_at,REMOTE_LEASE_MS)) {
        all_off(p,now); p->mode=MODE_MANUAL; p->remote_expired=true;
    }
    if (p->remote_expired) p->faults |= FAULT_REMOTE;
    if (p->mode == MODE_MANUAL && elapsed(now,p->activity_at,MANUAL_LEASE_MS)) all_off(p,now);
    uint8_t wanted=p->request;
    if (p->mode == MODE_AUTO) {
        if (!(p->valid & VALID_TEMP)) p->temp_fan=false;
        else if (s->temp_dC >= (int32_t)p->cfg.temp_on) p->temp_fan=true;
        else if (s->temp_dC <= (int32_t)p->cfg.temp_off) p->temp_fan=false;
        if (!(p->valid & VALID_CO2)) p->co2_fan=false;
        else if (s->co2_ppm >= p->cfg.co2_on) p->co2_fan=true;
        else if (s->co2_ppm <= p->cfg.co2_off) p->co2_fan=false;
        if (!(p->valid & VALID_LIGHT) || !(p->cfg.cal_flags & CAL_LIGHT)) p->lamp=p->shade=false;
        else {
            if (s->light_pct <= p->cfg.light_on) p->lamp=true;
            else if (s->light_pct >= p->cfg.light_off) p->lamp=false;
            if (s->light_pct >= p->cfg.shade_on) p->shade=true;
            else if (s->light_pct <= p->cfg.shade_off) p->shade=false;
        }
        if (!(p->valid & VALID_SOIL) || !(p->cfg.cal_flags & CAL_SOIL)) p->auto_pump=false;
        else if (s->soil_pct <= p->cfg.soil_on) p->auto_pump=true;
        else if (s->soil_pct >= p->cfg.soil_off) p->auto_pump=false;
        wanted=(uint8_t)((p->temp_fan||p->co2_fan ? OUT_FAN:0) |
            (p->auto_pump ? OUT_PUMP:0) | (p->lamp ? OUT_LAMP:0) | (p->shade ? OUT_SHADE:0));
    }
    /* In every mode the pump requires calibrated, live soil and a closed float switch. */
    if (!(p->valid & VALID_SOIL) || !(p->cfg.cal_flags & CAL_SOIL) || !s->water_ok ||
        p->pump_used_ms >= PUMP_BUDGET_MS) {
        wanted &= (uint8_t)~OUT_PUMP;
        p->request &= (uint8_t)~OUT_PUMP;
    }
    if ((p->outputs & OUT_PUMP) && elapsed(now,p->pump_since,p->cfg.pump_max_ms)) {
        wanted &= (uint8_t)~OUT_PUMP;
        p->request &= (uint8_t)~OUT_PUMP;
    }
    if (!(p->outputs & OUT_PUMP) && p->has_pumped && !elapsed(now,p->pump_stop,p->cfg.cooldown_ms)) {
        wanted &= (uint8_t)~OUT_PUMP;
        p->request &= (uint8_t)~OUT_PUMP;
    }
    /* A shade and grow lamp never run together, including manual commands. */
    if (wanted & OUT_SHADE) { wanted &= (uint8_t)~OUT_LAMP; p->request &= (uint8_t)~OUT_LAMP; }
    if ((p->outputs & OUT_PUMP) && !(wanted & OUT_PUMP)) { p->pump_stop=now; p->has_pumped=true; }
    if (!(p->outputs & OUT_PUMP) && (wanted & OUT_PUMP)) p->pump_since=now;
    p->outputs=wanted;
}

bool parse_u32(const char *text, uint32_t *value) {
    uint32_t n=0;
    if (!*text) return false;
    while (*text) {
        if (*text<'0'||*text>'9') return false;
        uint32_t d=(uint32_t)(*text++-'0');
        if (n>(UINT32_MAX-d)/10u) return false;
        n=n*10u+d;
    }
    *value=n; return true;
}
typedef struct { const char *name; size_t offset; } field_t;
#define FIELD(name,member) {name,offsetof(plant_config_t,member)}
static const field_t fields[]={
    FIELD("TEMP_ON",temp_on),FIELD("TEMP_OFF",temp_off),FIELD("SOIL_ON",soil_on),FIELD("SOIL_OFF",soil_off),
    FIELD("LIGHT_ON",light_on),FIELD("LIGHT_OFF",light_off),FIELD("SHADE_ON",shade_on),FIELD("SHADE_OFF",shade_off),
    FIELD("CO2_ON",co2_on),FIELD("CO2_OFF",co2_off),FIELD("PUMP_MS",pump_max_ms),FIELD("COOLDOWN_MS",cooldown_ms)
};
bool plant_threshold(plant_config_t *c, const char *name, uint32_t value) {
    plant_config_t candidate=*c;
    for (size_t i=0;i<sizeof(fields)/sizeof(fields[0]);++i) {
        if (strcmp(name,fields[i].name)) continue;
        *(uint32_t *)((uint8_t *)&candidate+fields[i].offset)=value;
        if (!plant_config_valid(&candidate)) return false;
        *c=candidate; return true;
    }
    return false;
}
void plant_command(plant_t *p, const char *line, uint32_t now, char *reply, size_t capacity) {
    char input[96]; char *arg[5]; size_t n=0;
    const char *answer="ERR COMMAND";
    if (strlen(line)>=sizeof(input)) { snprintf(reply,capacity,"ERR LENGTH\n"); return; }
    strcpy(input,line);
    char *cursor=input;
    while (*cursor) {
        while (*cursor==' ') ++cursor;
        if (!*cursor) break;
        if (n==5) { snprintf(reply,capacity,"ERR ARGS\n"); return; }
        arg[n++]=cursor;
        while (*cursor && *cursor!=' ') ++cursor;
        if (*cursor) *cursor++='\0';
    }
    uint32_t a=0,b=0;
    if (!n) return (void)snprintf(reply,capacity,"ERR EMPTY\n");
    if (!strcmp(arg[0],"STOP") && n==1) { plant_stop(p,now); answer="OK STOP"; }
    else if (!strcmp(arg[0],"PING") && n==1) {
        if (p->mode==MODE_REMOTE) { p->activity_at=now; answer="OK PONG"; }
        else answer="ERR MODE";
    } else if (!strcmp(arg[0],"MODE") && n==2) {
        plant_mode_t m=MODE_MANUAL;
        bool known=true;
        if (!strcmp(arg[1],"AUTO")) m=MODE_AUTO;
        else if (!strcmp(arg[1],"REMOTE")) m=MODE_REMOTE;
        else if (strcmp(arg[1],"MANUAL")) known=false;
        answer=known && plant_mode(p,m,now) ? "OK MODE" : "ERR MODE_OR_CAL";
    } else if (!strcmp(arg[0],"SET") && n==3 && parse_u32(arg[2],&a) && a<=1) {
        uint8_t mask=0;
        if (!strcmp(arg[1],"FAN")) mask=OUT_FAN;
        if (!strcmp(arg[1],"PUMP")) mask=OUT_PUMP;
        if (!strcmp(arg[1],"LAMP")) mask=OUT_LAMP;
        if (!strcmp(arg[1],"SHADE")) mask=OUT_SHADE;
        answer=p->mode==MODE_REMOTE && plant_manual(p,mask,a!=0,now) ? "OK REQUEST" : "ERR MODE_OR_DEVICE";
    } else if (!strcmp(arg[0],"THRESH") && n==3 && parse_u32(arg[2],&a)) {
        answer=p->mode==MODE_MANUAL && p->outputs==0 && p->request==0 &&
            plant_threshold(&p->cfg,arg[1],a) ? "OK THRESH RAM" : "ERR IDLE_OR_RANGE";
    } else if (!strcmp(arg[0],"CAL") && n==4 && parse_u32(arg[2],&a) && parse_u32(arg[3],&b)) {
        plant_config_t c=p->cfg;
        bool known=true;
        if (!strcmp(arg[1],"SOIL")) { c.soil_dry=a; c.soil_wet=b; c.cal_flags|=CAL_SOIL; }
        else if (!strcmp(arg[1],"LIGHT")) { c.light_dark=a; c.light_bright=b; c.cal_flags|=CAL_LIGHT; }
        else known=false;
        if (known && p->mode==MODE_MANUAL && p->outputs==0 && p->request==0 && plant_config_valid(&c)) {
            p->cfg=c; answer="OK CAL RAM";
        } else answer="ERR IDLE_OR_RANGE";
    } else if (!strcmp(arg[0],"DEFAULTS") && n==1 && p->mode==MODE_MANUAL && p->outputs==0 && p->request==0) {
        plant_defaults(&p->cfg); answer="OK DEFAULTS RAM";
    } else if (!strcmp(arg[0],"CFG") && n==1) {
        snprintf(reply,capacity,
            "CFG TEMP=%lu,%lu SOIL=%lu,%lu LIGHT=%lu,%lu SHADE=%lu,%lu CO2=%lu,%lu PUMP=%lu COOL=%lu CAL=%lu,%lu,%lu,%lu,%lu\n",
            (unsigned long)p->cfg.temp_on,(unsigned long)p->cfg.temp_off,(unsigned long)p->cfg.soil_on,(unsigned long)p->cfg.soil_off,
            (unsigned long)p->cfg.light_on,(unsigned long)p->cfg.light_off,(unsigned long)p->cfg.shade_on,(unsigned long)p->cfg.shade_off,
            (unsigned long)p->cfg.co2_on,(unsigned long)p->cfg.co2_off,(unsigned long)p->cfg.pump_max_ms,(unsigned long)p->cfg.cooldown_ms,
            (unsigned long)p->cfg.soil_dry,(unsigned long)p->cfg.soil_wet,(unsigned long)p->cfg.light_dark,
            (unsigned long)p->cfg.light_bright,(unsigned long)p->cfg.cal_flags);
        return;
    } else if (!strcmp(arg[0],"HELP") && n==1) {
        answer="OK GET CFG MODE SET PING STOP THRESH CAL DEFAULTS SAVE TIME; see docs/protocol.md";
    }
    snprintf(reply,capacity,"%s\n",answer);
}
static void numeric(char *out, size_t capacity, bool valid, int value) {
    if (valid) snprintf(out,capacity,"%d",value); else snprintf(out,capacity,"null");
}
void plant_telemetry(const plant_t *p, const plant_sensors_t *s, uint32_t now, uint32_t epoch,
                     uint32_t io_errors, char *out, size_t capacity) {
    char t[12],soil[12],light[12],co2[12];
    numeric(t,sizeof(t),(p->valid&VALID_TEMP)!=0,s->temp_dC);
    numeric(soil,sizeof(soil),(p->valid&VALID_SOIL)!=0 && (p->cfg.cal_flags&CAL_SOIL)!=0,s->soil_pct);
    numeric(light,sizeof(light),(p->valid&VALID_LIGHT)!=0 && (p->cfg.cal_flags&CAL_LIGHT)!=0,s->light_pct);
    numeric(co2,sizeof(co2),(p->valid&VALID_CO2)!=0,s->co2_ppm);
    snprintf(out,capacity,
        "{\"v\":1,\"ms\":%lu,\"epoch\":%lu,\"mode\":%u,\"t_dC\":%s,\"soil\":%s,\"light\":%s,\"co2\":%s,\"raw\":[%u,%u],\"valid\":%u,\"cal\":%lu,\"out\":%u,\"fault\":%lu,\"water\":%u,\"pump_ms\":%lu,\"io_errors\":%lu}\n",
        (unsigned long)now,(unsigned long)epoch,(unsigned)p->mode,t,soil,light,co2,
        s->soil_raw,s->light_raw,(unsigned)p->valid,(unsigned long)p->cfg.cal_flags,(unsigned)p->outputs,
        (unsigned long)p->faults,(unsigned)s->water_ok,(unsigned long)p->pump_used_ms,(unsigned long)io_errors);
}
int line_feed(line_reader_t *r, char ch) {
    if (ch=='\r') return 0;
    if (ch=='\n') {
        if (r->discard) { r->used=0; r->discard=false; return -1; }
        r->text[r->used]='\0'; r->used=0; return 1;
    }
    if ((unsigned char)ch<32 || (unsigned char)ch>126 || r->used>=sizeof(r->text)-1) { r->discard=true; return 0; }
    if (!r->discard) r->text[r->used++]=ch;
    return 0;
}
