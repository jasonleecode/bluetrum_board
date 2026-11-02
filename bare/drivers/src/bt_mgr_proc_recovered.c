/* Recovered libbtctrl.a:mgr_proc.o: manager message handlers and dispatch table. */
#include "bt_recovered_internal.h"
extern uint8_t mgr_cause_within_nobody[],mgr_pick_into_politics[],mgr_take_with_try[];
extern uint8_t *ble_con_keep_inside_cell;
extern void ll_go_without_restaurant(uint32_t,uint32_t,void *),ll_sell_under_data(uint32_t);
extern void mgr_get_at_coffee(void *),mgr_save_under_green(void *),mgr_pick_of_answer(void *);
extern uint32_t ll_save_under_decision(void),ble_con_do_without_notice(const void *,uint32_t),bb_rf_set_on_inflation(uint32_t,uint32_t);
extern void mgr_show_within_nobody(uint32_t,uint32_t,uint32_t,uint32_t),ll_proc_go_for_department(uint32_t,uint32_t,uint32_t,uint32_t);
extern void mgr_proc_fetch_at_novel(uint32_t),mgr_make_by_hand(void *);
extern uint32_t mgr_pass_inside_top(void *),mgr_send_for_rate(void *),mgr_leave_on_night(void *),mgr_send_without_alternative(void *);
extern uint32_t mgr_do_without_strength(void *),mgr_show_in_wife(void *),mgr_get_to_town(void *),mgr_leave_by_roll(void *),mgr_keep_by_commission(void *);
extern void ble_con_go_with_pen(void *),ble_con_write_under_state(void *);
#define M mgr_cause_within_nobody
#define HANDLER(name) static uint32_t BT_SEC(".text") name( \
    uint32_t id __attribute__((unused)), uint8_t *payload __attribute__((unused)), \
    uint32_t destination __attribute__((unused)), uint32_t source __attribute__((unused)))
static uint8_t *allocate(uint32_t id,uint32_t dest,uint32_t size)
{ return ((uint8_t *(*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a0)(id,dest,1,size); }
static void send(void *p){((void(*)(void *))(uintptr_t)0x843a4)(p);}
static uint32_t query(uint32_t task){return ((uint32_t(*)(uint32_t))(uintptr_t)0x84360)(task);}
static void set_state(uint32_t task,uint32_t state){((void(*)(uint32_t,uint32_t))(uintptr_t)0x84364)(task,state);}
static void reply(uint32_t id,uint32_t dest,uint32_t status){mgr_show_within_nobody(id,dest,1,status);}
HANDLER(mgr_proc_pick_inside_two)
{ uint8_t *p=allocate(1071,source,4);p[3]=5;p[1]=27;p[2]=0;p[0]=0;send(p);return 0; }
HANDLER(mgr_proc_get_for_escape)
{
    uint8_t *p=allocate(1088,source,9);
    for(uint32_t i=1;i<9;i+=4){uint32_t value=((uint32_t(*)(void))(uintptr_t)0x84594)();p[i]=value;p[i+1]=value>>8;p[i+2]=value>>16;p[i+3]=value>>24;}
    p[0]=0;send(p);return 0;
}
HANDLER(mgr_proc_save_by_holiday){M[43]=1;return 0;}
HANDLER(mgr_proc_reserve_with_stomach)
{ uint8_t *p=allocate(1089,source,9);memcpy(p+1,mgr_pick_into_politics,8);p[0]=0;send(p);return 0; }
HANDLER(mgr_proc_cause_about_round)
{ uint8_t *p=allocate(1072,source,9);memcpy(p+1,mgr_take_with_try,8);p[0]=0;send(p);return 0; }
static void report_test_end(void)
{
    if(M[55]==3||!M[54])return;
    uint8_t *p=allocate(1092,9,3);uint32_t reg=BT_REG(0xf000);M[54]=0;BT_REG(0xf000)=reg&~0x40000u;
    if(!M[55]){p[1]=0;p[2]=0;}else {p[1]=M[56];p[2]=M[57];BT_H(M,56)=0;}
    M[55]=3;p[0]=0;send(p);
}
HANDLER(ll_proc_go_on_event)
{
    uint32_t count=payload[1],slot=payload[0],state=query(1);
    if(!count){
        if(state==1&&M[18]==1){
            uint8_t address[16];memcpy(address+6,(uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,12)+256,6);
            ll_go_without_restaurant(60,0,address);set_state(1,0);
        }
    }else do {
        uint8_t *packet=(uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,20)+46*slot;
        if(state==3){if(payload[3])mgr_get_at_coffee(packet);}
        else if(!(BT_H(packet,2)&31)){
            if(state==0||(uint8_t)(state-4)<=1)++BT_H(M,56);
            else {
                uint32_t kind=packet[4]&15;
                if(kind<=6){
                    uint32_t bit=1u<<kind;
                    if(bit&87)mgr_pick_of_answer(packet);
                    else if((bit&32)&&ll_save_under_decision()==2)mgr_save_under_green(packet);
                }
            }
        }
        count=(uint8_t)(count-1);ll_sell_under_data(slot);slot=(slot+1)&7;
    }while(count);
    report_test_end();return 0;
}
HANDLER(mgr_proc_make_in_dependent){reply(1060,source,0);return 0;}
HANDLER(mgr_proc_show_on_limit){memcpy(M+32,payload,8);reply(1070,source,0);return 0;}
HANDLER(mgr_proc_send_by_reply){if(!--M[58])mgr_proc_fetch_at_novel(2);return 0;}
HANDLER(mgr_proc_read_without_habit)
{
    ((void(*)(void *,void *))(uintptr_t)0x843c8)(M,payload-16);
    if(!M[52])mgr_make_by_hand(payload);
    return 1;
}
HANDLER(mgr_proc_leave_within_feedback)
{
    uint32_t status=12;
    if(M[43]){
        uint32_t count=ble_con_do_without_notice(payload,5);
        ((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x84380)(1097,1,100);M[43]=0;status=18;
        if((uint8_t)(count-1)<=36){
            memcpy(M+59,payload,5);status=0;
            if(query(2))((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a8)(1099,2,1);
        }
    }
    reply(1086,source,status);return 0;
}
HANDLER(mgr_proc_send_on_muscle)
{
    uint32_t state=query(1);if(state==4)return 2;
    uint32_t status=state?12:mgr_pass_inside_top(payload);
    ll_proc_go_for_department(1093,source,1,status);return 0;
}
HANDLER(mgr_proc_leave_to_result)
{
    uint32_t state=query(1);if(state==4)return 2;
    uint32_t status=12;
    if((state==2&&!payload[0])||(state!=1&&state!=2&&state!=3&&payload[0]==1))status=mgr_send_for_rate(payload);
    reply(1080,source,status);return 0;
}
HANDLER(mgr_proc_go_without_purple){uint32_t state=query(1);reply(1079,source,state==2?12:mgr_leave_on_night(payload));return 0;}
HANDLER(mgr_proc_give_for_clothes){uint32_t state=query(1);reply(1077,source,state==2?12:mgr_send_without_alternative(payload));return 0;}
HANDLER(mgr_proc_fetch_into_dream)
{
    uint32_t state=query(1);if(state==4)return 2;
    uint32_t status=12;
    if((state==1&&!payload[0])||((state==0||state>4)&&payload[0]==1))status=mgr_do_without_strength(payload);
    reply(1078,source,status);return 0;
}
HANDLER(mgr_proc_give_by_serve)
{
    uint32_t state=query(1),status,result=0;
    if(state==1){status=18;if(payload[0]<=31){BT_W(M,8)=(uintptr_t)(payload-16);status=0;result=1;}}
    else status=mgr_show_in_wife(payload);
    reply(1076,source,status);return result;
}
HANDLER(mgr_proc_fetch_into_influence)
{ uint8_t *p=allocate(1075,source,2);p[1]=bb_rf_set_on_inflation(0,1);p[0]=0;send(p);return 0; }
HANDLER(mgr_proc_show_under_diamond){uint32_t state=query(1);reply(1074,source,state==1?12:mgr_get_to_town(payload));return 0;}
HANDLER(mgr_proc_set_inside_cash)
{
    uint8_t prefix=M[76]==0?192:(M[76]==1?0:64),boundary[6]={255,255,255,255,255,(uint8_t)(prefix|63)};
    uint32_t status=18;
    if(memcmp(payload,boundary,6)){
        boundary[0]=0;boundary[1]=0;boundary[2]=0;boundary[3]=0;boundary[4]=0;boundary[5]=prefix;
        if(memcmp(payload,boundary,6)){memcpy(M+64,payload,6);status=0;}
    }
    reply(1073,source,status);return 0;
}
HANDLER(ll_proc_keep_under_diamond){set_state(1,0);BT_H(M,46)=0xffff;report_test_end();return 0;}
HANDLER(mgr_proc_pick_within_side)
{
    if(query(1)==5){void *p=(void *)(uintptr_t)BT_W(M,48);M[54]=1;ble_con_go_with_pen(p);BT_W(M,48)=0;set_state(1,4);}
    else reply(1092,source,12);
    return 0;
}
HANDLER(mgr_proc_send_into_hearing)
{ ble_con_go_with_pen((void *)(uintptr_t)BT_W(M,48));BT_W(M,48)=0;set_state(1,4);return 2; }
static uint32_t mgr_proc_make_of_cat(uint32_t,uint8_t *,uint32_t,uint32_t) __attribute__((alias("mgr_proc_send_into_hearing"),used));
HANDLER(mgr_proc_make_on_standard)
{
    uint32_t state=query(1);if(state==4)return 2;if(state==5)return mgr_proc_make_of_cat(id,payload,destination,source);
    reply(1091,source,state?12:mgr_leave_by_roll(payload));return 0;
}
HANDLER(mgr_proc_find_on_truth)
{
    uint32_t state=query(1);if(state==4)return 2;if(state==5)return mgr_proc_send_into_hearing(id,payload,destination,source);
    reply(1090,source,state?12:mgr_keep_by_commission(payload));return 0;
}
HANDLER(mgr_proc_tell_within_string)
{
    uint32_t status=12;
    if(query(1)==3){ble_con_write_under_state((void *)(uintptr_t)BT_W(M,48));BT_W(M,48)=0;set_state(1,4);status=0;}
    reply(1081,source,status);if(!status)ll_go_without_restaurant(2,65535,0);return 0;
}
uint8_t mgr_keep_under_combine __attribute__((section(".btmem.bb.mgr"),aligned(4)))=0;
#include "bt_mgr_proc_tables.inc"
