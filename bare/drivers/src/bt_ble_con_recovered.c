/* Recovered libbtctrl.a:ble_con.o: link hardware records and connection setup. */
#include "bt_recovered_internal.h"
extern uint8_t ble_con_show_of_beat[];
extern uint8_t *ble_con_keep_inside_cell;
extern void bt_get_local_bd_addr(void *),ble_con_fetch_at_pressure(uint32_t);
extern uint8_t *ble_con_show_without_uncle(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
extern uint8_t *ble_con_cause_with_cold(uint32_t,uint32_t,uint32_t,uint32_t);
extern uint8_t *ble_con_give_to_series(void *,uint32_t,uint32_t,uint32_t,uint32_t,void *);
extern uint8_t *ble_con_leave_of_dance(const void *,const void *,void *,uint32_t);
extern void ble_con_keep_on_muscle(void *,uint32_t),ble_con_make_in_spirit(void *,uint32_t),ble_con_give_within_suit(const void *,void *);
extern uint32_t ble_con_do_without_notice(const void *,uint32_t),ble_con_give_at_black(void *);
extern void bb_rf_go_inside_log(uint32_t);
extern void ble_con_keep_by_success(void),ble_con_show_to_slip(void),ble_con_send_at_age(void),ble_con_do_by_fill(void),ble_con_tell_by_advantage(void);
uint32_t ble_con_make_on_name[9] __attribute__((common,aligned(4)));
static const uintptr_t callbacks[7] __attribute__((section(".rodata.bb.ble"),aligned(4),used))={
    (uintptr_t)ble_con_keep_by_success,(uintptr_t)ble_con_show_to_slip,(uintptr_t)ble_con_send_at_age,0,
    (uintptr_t)ble_con_do_by_fill,(uintptr_t)ble_con_tell_by_advantage,0
};
#define F BT_SEC(".text")
#define H BT_H
#define W BT_W
#define RADIO(i) ((uint8_t *)(uintptr_t)ble_con_make_on_name[(i)+1])
#define ROM1(a,p) ((void(*)(void *))(uintptr_t)(a))(p)
#define ROM2(a,p,q) ((void(*)(void *,uintptr_t))(uintptr_t)(a))(p,(uintptr_t)(q))
static uint16_t le16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static void put16(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;}
void F ble_con_show_on_border(uint8_t *p)
{
    uint32_t value=BT_REG(0xf024);p[0]=value;p[1]=value>>8;p[2]=value>>16;p[3]=value>>24;
    value=BT_REG(0xf028);p[4]=value;p[5]=value>>8;
}
void F ble_con_make_on_burn(const uint8_t *p,uint32_t kind)
{
    BT_REG(0xf024)=le16(p)|((uint32_t)le16(p+2)<<16);
    BT_REG(0xf028)=(BT_REG(0xf028)&0xffff0000)|le16(p+4);
    BT_REG(0xf028)=(BT_REG(0xf028)&0xfffeffff)|(kind<<16);
}
void F ble_leave_about_television(uint32_t reset)
{
    for(uint32_t i=0;i<8;++i)ble_con_make_on_name[i+1]=0x11298+78*i;
    BT_B(ble_con_make_on_name,0)=14;H(ble_con_make_on_name,2)=1;BT_B(ble_con_make_on_name,1)=0;
    ROM1(0x84620,(void *)callbacks);ROM2(0x84624,ble_con_make_on_name,ble_con_show_of_beat);
    BT_REG(0xf000)=(BT_REG(0xf000)&~240u)|224;
    BT_REG(0xf000)=(BT_REG(0xf000)&~4096u)|4096;
    BT_REG(0xf000)=(BT_REG(0xf000)&~2048u)|2048;
    BT_REG(0xf0f0)=150;BT_REG(0xf00c)=314;
    BT_REG(0xf000)&=~0x200000u;BT_REG(0xf000)&=~0x100000u;
    uint8_t address[6];bt_get_local_bd_addr(address);
    BT_REG(0xf024)=address[5]|((uint32_t)address[4]<<8)|((uint32_t)address[3]<<16)|((uint32_t)address[2]<<24);
    BT_REG(0xf028)=address[1]|((uint32_t)address[0]<<8);
    BT_REG(0xf000)|=1024;BT_REG(0xf090)=7;BT_REG(0xf0b0)=1624;BT_REG(0xf0b4)=1642;BT_REG(0xf0b8)=771;BT_REG(0xf110)=32798;
    uint8_t *hw=RADIO(1);H(hw,2)=0xbed6;H(hw,4)=0x8e89;H(hw,6)=0x5555;hw[8]=85;H(hw,12)=1024;
    H(hw,24)=0;H(hw,26)=0;H(hw,28)=0;
    BT_REG(0xf02c)=(W(ble_con_keep_inside_cell,20)+46*ble_con_keep_inside_cell[24]-0x1000)&0x3fff;
    for(uint32_t i=0;i<16;++i)H(0x11170,2*i)=0;
    ble_con_fetch_at_pressure(reset);BT_REG(0xf000)=(BT_REG(0xf000)&~7u)|2;BT_REG(0xf000)|=256;
}
void F bb_pick_under_strike(void)
{
    BT_REG(0xf000)&=~256u;for(uint32_t i=0;i<16;++i)H(0x11170,2*i)=0;
    BT_REG(0xf00c)=0;BT_REG(0xf018)=0xffffffff;
}
uint8_t *F ble_con_get_of_shoe(uint8_t *parameters,void *packet)
{
    uint32_t interval=H(parameters,0);
    if(interval<=6)H(parameters,0)=6;
    else if((int32_t)interval-(int32_t)H(parameters,2)<=5)H(parameters,2)=interval-6;
    uint8_t *p=ble_con_show_without_uncle(1,H(parameters,2),H(parameters,0)>>1,H(parameters,0),0);
    uint8_t *hw=RADIO(1);H(hw,0)=packet?9:8;H(hw,10)=0x8027;hw[9]=parameters[4];hw[8]=85;
    H(hw,14)=0x8000|H(p,58);H(hw,22)=H(p,58);H(hw,76)=0x4e0e;
    if(packet){ROM2(0x8460c,p,packet);ROM1(0x84608,p);}ROM1(0x84648,p);return p;
}
void F ble_con_write_under_state(void *p){ROM2(0x84668,p,0x1000000);}
uint8_t *F ble_con_cause_within_youth(const uint8_t *parameters,uint8_t *packet,uint32_t handle)
{
    uint32_t minimum=(uint16_t)(le16(parameters+13)*2),window=le16(parameters+2);
    int32_t limit=(int32_t)minimum-7;
    if((int32_t)window>=limit)window=(uint16_t)(minimum-8);
    uint32_t duration=(le16(parameters+21)+1)&~1u;
    if((int32_t)duration>(limit&~1))duration=(uint16_t)(limit&~1);
    else duration=(uint16_t)duration;
    if(!duration)duration=2;
    uint8_t *p=ble_con_show_without_uncle(1,(uint16_t)(window>=duration?window:duration),minimum,
                                         (uint16_t)(le16(parameters+15)*2),le16(parameters+17));
    uint32_t scan=H(p,58)>=window?window:H(p,58),connection=H(p,58)>=duration?duration:H(p,58);
    uint8_t *hw=RADIO(1);H(hw,0)=15;H(hw,10)=0x8027;
    for(uint32_t i=0;i<6;i+=2)H(hw,32+i)=le16(parameters+6+i);
    H(hw,38)=parameters[5];hw[9]=parameters[4];hw[8]=85;H(hw,14)=0x8000|scan;
    uint32_t interval=H(p,60)>>1;H(hw,20)=interval-2;H(hw,22)=scan;H(hw,76)=0x5212;
    uint8_t *tx=(uint8_t *)(uintptr_t)W(ble_con_keep_inside_cell,12)+42*H(packet,4);
    H(tx,14)=interval;tx[11]=2;ROM2(0x8460c,p,packet);
    hw=RADIO(handle);H(hw,0)=2;H(hw,10)=0x8000|(((uint32_t)tx[25]<<8)&0x1f00);
    H(hw,2)=le16(tx+4);H(hw,4)=le16(tx+6);H(hw,6)=le16(tx+8);H(hw,30)=0;hw[8]=tx[10];
    H(hw,20)=connection-1;H(hw,22)=connection-1;H(hw,12)=1024;
    H(hw,24)=H(tx,20);H(hw,26)=H(tx,22);
    uint32_t count=ble_con_do_without_notice(tx+20,5);H(hw,28)=(count<<8)|tx[24];
    H(RADIO(handle),76)=0x7616;ROM1(0x84648,p);return p;
}
void F ble_con_find_of_blow(void *p,uint32_t handle){ble_con_keep_on_muscle(p,handle);}
void F ble_con_find_inside_service(void *old,const uint8_t *parameters,uint8_t *result)
{
    uint32_t duration=(le16(parameters+10)+1)&~1u;if(duration<2)duration=2;
    uint8_t instant[8];uint8_t *p=ble_con_give_to_series(old,(uint16_t)duration,(uint16_t)(le16(parameters+2)*2),
        (uint16_t)(le16(parameters+4)*2),le16(parameters+6),instant);
    uint8_t *hw=RADIO(le16(parameters));uint32_t length=(uint16_t)(H(p,58)-1);H(hw,20)=length;H(hw,22)=length;
    put16(result+8,le16(parameters+8));put16(result+4,H(p,60)>>1);put16(result+6,le16(parameters+6));
    result[1]=instant[4];result[2]=instant[0];result[3]=instant[1];result[10]=instant[2];result[11]=instant[3];
}
uint32_t F ble_con_keep_within_broad(void *p){return ble_con_give_at_black(p);}
uint8_t *F ble_con_make_on_street(const uint8_t *parameters,void *packet,void *response,uint32_t flags)
{
    uint32_t window=parameters[10]==1?1250:1500,kind=parameters[10]==1?0:2;
    uint8_t *p=ble_con_cause_with_cold(1,H(parameters,4),H(parameters,6),kind),*hw=RADIO(1);
    H(hw,0)=4;H(hw,10)=0x8027;hw[9]=parameters[9];hw[8]=85;H(hw,30)=0;H(hw,14)=0x8000|H(p,58);
    H(hw,22)=2400;H(hw,12)=flags|1024;BT_REG(0xf0f0)=150;BT_REG(0xf090)=parameters[8];BT_REG(0xf0a0)=window;
    H(hw,76)=0x4e0e;p[81]=flags;ROM2(0x8460c,p,packet);if(response)ROM2(0x8460c,p,response);
    ROM1(0x84608,p);ROM1(0x84648,p);return p;
}
void F ble_con_find_inside_official(void *p){ROM2(0x84668,p,0x2000000);}
void F ble_con_cause_about_mud(void *p,uint32_t instant){ROM1(0x84660,p);ble_con_make_in_spirit(p,instant);}
void F ble_con_sell_by_champion(void *p,const void *parameters){ROM1(0x84660,p);ble_con_give_within_suit(parameters,p);}
uint8_t *F ble_con_take_inside_pace(const void *parameters,const uint8_t *request,void *old,uint32_t handle)
{
    uint8_t *hw=RADIO(handle);H(hw,0)=3;H(hw,10)=0x8000|(((uint32_t)request[33]<<8)&0x1f00);
    H(hw,2)=le16(request+12);H(hw,4)=le16(request+14);H(hw,6)=le16(request+16);hw[8]=request[18];H(hw,12)=1024;
    H(hw,22)=H(request,22)*2-5;H(hw,24)=H(request,28);H(hw,26)=H(request,30);
    uint32_t count=ble_con_do_without_notice(request+28,5);H(hw,28)=(count<<8)|request[32];
    H(RADIO(handle),76)=0x7616;return ble_con_leave_of_dance(parameters,request,old,handle);
}
void F ble_con_take_by_join(void *p){ROM1(0x84670,p);ROM2(0x84668,p,0);}
void F ble_con_go_with_pen(void *p){if(p)ROM2(0x84668,p,0x4000000);bb_rf_go_inside_log(1);}
static uint32_t channel_index(uint32_t channel)
{
    if(channel==0)return 37;
    if(channel==12)return 38;
    if(channel==39)return 39;
    return (channel-(channel<=11?1:2))&255;
}
uint8_t *F ble_con_reserve_on_option(void *packet,uint32_t channel)
{
    uint8_t *p=ble_con_cause_with_cold(1,0,1,0),*hw=RADIO(1);
    H(hw,0)=28;H(hw,2)=0x4129;H(hw,4)=0x7176;H(hw,12)&=0xff00;H(hw,76)=0x4e0e;
    H(hw,10)=(H(hw,10)&~63u)|channel_index(channel);
    ROM2(0x8460c,p,packet);ROM1(0x84608,p);ROM1(0x84648,p);return p;
}
uint8_t *F ble_con_do_by_wall(uint32_t channel)
{
    uint8_t *p=ble_con_cause_with_cold(1,0,1,0),*hw=RADIO(1);
    H(hw,0)=29;H(hw,14)=0x804b;H(hw,2)=0x4129;H(hw,4)=0x7176;H(hw,76)=0x4e0e;
    H(hw,10)=(H(hw,10)&~63u)|channel_index(channel);bb_rf_go_inside_log(0);ROM1(0x84648,p);return p;
}
