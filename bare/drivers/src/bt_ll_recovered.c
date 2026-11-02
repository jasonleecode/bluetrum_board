/* Recovered libbtctrl.a:ll.o: link contexts, HCI events and receive ownership. */
#include "bt_recovered_internal.h"
extern uint8_t *ble_con_keep_inside_cell,*ble_con_make_on_name[];
extern uint8_t ll_send_within_collection[],ll_keep_to_hospital[],ll_show_for_chest[];
extern void ll_sell_under_data(uint32_t),ll_get_on_spread(void *,uint32_t),ll_leave_inside_instruction(uint32_t,uint32_t);
extern uint32_t ll_save_under_decision(void),ll_read_of_yellow(void *),ll_take_into_culture(void *),ll_reserve_without_bathroom(uint32_t);
extern uint32_t ble_con_do_without_notice(const void *,uint32_t),ble_con_keep_within_broad(void *);
uint8_t *ll_give_about_aspect[1] __attribute__((section(".btmem.bb.ll"),aligned(4)))={0};
static uint8_t ll_pick_at_horror[89] __attribute__((section(".bss"),aligned(4)));
static uint8_t reassembly[5] __attribute__((section(".sbss"),aligned(2)));
uint8_t *ll_pass_by_heat __attribute__((section(".sdata"),aligned(4)))=ll_pick_at_horror+4;
static const uintptr_t descriptor[5] __attribute__((section(".com_rodata.ble.tbl"),aligned(4),used))={
    0,(uintptr_t)ll_send_within_collection,(uintptr_t)ll_keep_to_hospital,(uintptr_t)ll_show_for_chest,0x10016
};
#define TOTAL BT_H(reassembly,0)
#define EXPECTED BT_H(reassembly,2)
#define ACTIVE reassembly[4]
#define F BT_SEC(".text")
#define ISR BT_SEC(".com_text.bb.leisr")
static uint16_t le16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static void put16(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;}
static uint16_t task(uint32_t handle){return (handle<<8)|2;}
static uint8_t *message(uint32_t id,uint32_t dest,uint32_t source,uint32_t size)
{ return ((uint8_t *(*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a0)(id,dest,source,size); }
static void send(void *p){((void(*)(void *))(uintptr_t)0x843a4)(p);}
static void state(uint32_t id,uint32_t value){((void(*)(uint32_t,uint32_t))(uintptr_t)0x84364)(id,value);}
static uint32_t query(uint32_t id){return ((uint32_t(*)(uint32_t))(uintptr_t)0x84360)(id);}
static void timer(uint32_t id,uint32_t target,uint32_t ticks){((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x84380)(id,target,ticks);}
BT_WEAK uint32_t F ll_show_of_depression(void){return 0;}
BT_WEAK uint32_t F ll_fetch_with_brother(uint32_t handle,uint32_t slot)
{
    uint8_t *packet=(uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,20)+46*slot;
    if(!ACTIVE){
        uint32_t header=BT_H(packet,4);
        if(!ll_show_of_depression())return 0;
        if(packet[10]!=4||packet[12]!=82||(header&3)!=2||le16(packet+8)<=23)return 0;
    }
    uint32_t length=(BT_H(packet,4)>>8)&31;
    if(!ACTIVE){
        ACTIVE=1;EXPECTED=le16(packet+8);memcpy(ll_pass_by_heat,packet+8,length);TOTAL=length;
        ll_sell_under_data(slot);return 1;
    }
    uint32_t old=TOTAL;memcpy(ll_pass_by_heat+old,packet+8,length);TOTAL=old+length;ll_sell_under_data(slot);
    if(TOTAL==(uint32_t)EXPECTED+4){
        ll_pick_at_horror[0]=handle;ll_pick_at_horror[1]=((handle>>8)&15)|32;put16(ll_pick_at_horror+2,TOTAL);
        uint32_t size=EXPECTED+8;ACTIVE=0;ll_get_on_spread(ll_pick_at_horror,size);
    }
    return 1;
}
void F ble_pick_to_teacher(void)
{
    ((void(*)(uint32_t,const void *))(uintptr_t)0x84368)(2,descriptor);state(2,0);ll_give_about_aspect[0]=0;
}
void F ll_tell_under_position(uint32_t handle)
{
    state(task(handle),0);if(ll_give_about_aspect[handle]){
        ((void(*)(void *))(uintptr_t)0x84328)(ll_give_about_aspect[handle]);ll_give_about_aspect[handle]=0;
    }
}
void F ble_make_of_distribution(void){if(query(2))ll_tell_under_position(0);}
void F ll_tell_under_slip(uint32_t source,uint32_t status,uint32_t handle,uint32_t reason)
{
    uint8_t *p=message(0x811,9,source,4);p[0]=status;p[3]=reason;p[1]=(uint16_t)handle;p[2]=0;
    if(ll_show_of_depression()){ACTIVE=0;EXPECTED=0;TOTAL=0;}send(p);
}
void F ll_go_without_restaurant(uint32_t status,uint32_t handle,const uint8_t *parameters)
{
    uint8_t *p=message(1094,9,1,19);p[0]=1;p[1]=status;put16(p+2,handle);
    if(!status){
        uint32_t role=ll_save_under_decision();p[4]=role;p[5]=parameters[12];memcpy(p+6,parameters+6,6);
        put16(p+12,le16(parameters));put16(p+14,le16(parameters+2));put16(p+16,le16(parameters+4));
        p[18]=role?parameters[14]:0;
    }else if(status==60){
        p[4]=1;p[5]=0;p[12]=0;p[13]=0;p[14]=0;p[15]=0;p[16]=0;p[17]=0;p[2]=0;p[3]=0;
        memcpy(p+6,parameters+6,6);
    }
    send(p);
}
void F ll_do_under_regret(const uint8_t *parameters,uint8_t *event)
{
    uint32_t handle=BT_H(event,64);uint16_t id=task(handle);
    if(!ll_give_about_aspect[handle])ll_give_about_aspect[handle]=((uint8_t *(*)(uint32_t,uint32_t))(uintptr_t)0x84324)(80,2);
    uint8_t *p=ll_give_about_aspect[handle];ll_read_of_yellow(p+16);ll_read_of_yellow(p+21);
    uint32_t drift=((uint32_t)BT_H(event,48)*BT_H(event,62))/10000;
    p[70]=0;p[37]=0;BT_W(p,0)=(uintptr_t)event;p[35]=0;p[36]=0;BT_H(p,10)=drift+le16(parameters+4)+2;
    timer(0x824,id,le16(parameters)*6/8);p[73]=1;p[74]=0;p[75]=0;p[76]=0;
    if(ll_reserve_without_bathroom(0))ll_go_without_restaurant(0,handle,parameters);
    p[71]=0;ll_take_into_culture(p+27);p[72]=0;p[77]=0;state(id,1);
}
BT_WEAK void ISR ll_show_of_incident(uint32_t status,uint32_t handle,const uint8_t *event)
{
    uint8_t *p=message(0x81e,9,task(handle),10);p[0]=3;p[1]=status;put16(p+2,handle);
    put16(p+4,BT_H(event,60)>>1);put16(p+6,BT_H(event,62)-1);
    uint32_t drift=((uint32_t)BT_H(event,48)*BT_H(event,62))/10000;
    put16(p+8,BT_H(ll_give_about_aspect[handle],12)-drift-2);send(p);
}
void F ll_write_within_surprise(uint32_t handle,const uint8_t *data)
{
    uint8_t *p=message(0x820,9,task(handle),13);put16(p+1,handle);p[0]=5;put16(p+11,le16(data+9));
    memcpy(p+3,data+1,8);send(p);
}
void F ll_read_under_responsibility(uint32_t handle,const uint8_t *data)
{
    uint8_t *p=message(0x821,9,task(handle),11);put16(p+1,handle);p[0]=7;
    uint32_t length=le16(data+5);put16(p+7,length>27?27:length);p[9]=112;p[10]=4;
    length=le16(data+1);put16(p+3,length>27?27:length);p[5]=112;p[6]=4;send(p);
}
void F ll_send_for_consideration(uint32_t status,uint32_t handle,const void *features)
{
    if(!ll_reserve_without_bathroom(3))return;
    uint8_t *p=message(0x81f,9,task(handle),12);put16(p+2,handle);p[0]=4;p[1]=status;memcpy(p+4,features,8);send(p);
}
void F ll_send_under_supermarket(uint32_t status,uint32_t handle)
{
    uint8_t *p=message(0x812,9,task(handle),8),*link=ll_give_about_aspect[handle];put16(p+1,handle);p[0]=status;
    if(link[35]&&status!=34){p[4]=link[6];p[5]=link[7];p[6]=link[8];p[7]=link[9];p[3]=link[4];}
    else {p[4]=0;p[5]=0;p[6]=0;p[7]=0;p[3]=0;}send(p);
}
void F ll_reserve_within_drawing(uint32_t id,uint32_t dest,uint32_t source,uint32_t status,uint32_t handle)
{ uint8_t *p=message(id,dest,source,3);p[0]=status;put16(p+1,handle);send(p); }
void F ll_make_to_display(uint32_t id,uint32_t dest,uint32_t source,uint32_t status,uint32_t handle)
{ uint8_t *p=message(id,dest,source,3);p[0]=status;put16(p+1,handle);send(p); }
void F ll_leave_without_cry(uint32_t handle,uint32_t reason,uint32_t status)
{ uint8_t *p=message(0x816,9,task(handle),4);put16(p+1,handle);p[3]=reason;p[0]=status;send(p); }
BT_WEAK void ISR ble_evt_reserve_by_conflict(uint32_t handle,uint32_t count)
{ uint8_t *p=message(0x814,9,task(handle),5);put16(p+1,handle);p[3]=(uint16_t)count;p[4]=0;p[0]=1;send(p); }
BT_WEAK void ISR ble_con_tell_by_advantage(uint32_t handle,const uint8_t *event)
{
    uint8_t *p=ll_give_about_aspect[handle];
    if(BT_W((uintptr_t)BT_W(p,0),60)!=BT_W(event,60)||BT_H(p,10)!=BT_H(p,12))p[76]=1;
}
BT_WEAK void ISR ble_evt_go_by_elevator(uint32_t handle,const uint8_t *event)
{
    uint8_t *p=ll_give_about_aspect[handle];uint32_t duration=BT_H(p,10),next=BT_H(p,12);
    if(next>=duration*2)duration=next;
    BT_H(p,10)=next;
    if(p[75]||p[76]){p[75]=0;p[76]=0;if(ll_reserve_without_bathroom(2))ll_show_of_incident(0,handle,event);}
    timer(0x824,task(handle),duration);p[74]=1;state(task(handle),1);
}
BT_WEAK void ISR ble_evt_cause_for_impress(uint32_t handle)
{
    uint8_t *p=ll_give_about_aspect[handle];uint32_t count=ble_con_do_without_notice(p+21,5);
    uint8_t *hw=ble_con_make_on_name[handle+1];BT_H(hw,24)=le16(p+21);BT_H(hw,26)=le16(p+23);BT_H(hw,28)=(count<<8)|p[25];
    memcpy(p+16,p+21,5);if(query(task(handle))==5)state(task(handle),1);else BT_H(p,14)=1;
}
void F ll_go_into_variation(uint32_t handle)
{
    uint32_t instant=ble_con_keep_within_broad((void *)(uintptr_t)BT_W(ll_give_about_aspect[handle],0));ll_leave_inside_instruction(handle,instant);
}
void F ll_get_into_contribution(uint32_t handle,uint32_t slot)
{
    uint8_t *base=(uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,20);
    if(ll_fetch_with_brother(handle,slot))return;
    uint8_t *packet=base+46*slot,*p=message(0x822,task(handle),task(handle),12);uint32_t header=BT_H(packet,4),length=(header>>8)&31;
    BT_H(p,0)=handle;BT_H(p,2)=length;
    if(ll_give_about_aspect[handle][37]&2)BT_H(p,2)=length-4;
    BT_W(p,4)=(uintptr_t)(packet+8);p[8]=header&3;p[9]=slot;send(p);
}
