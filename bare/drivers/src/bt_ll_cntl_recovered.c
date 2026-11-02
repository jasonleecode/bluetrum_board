/* Recovered libbtctrl.a:ll_cntl.o: link control PDUs and procedure transitions. */
#include "bt_recovered_internal.h"
extern uint8_t *ble_con_keep_inside_cell,*ll_give_about_aspect[],*ble_con_make_on_name[];
extern void ll_cntl_leave_about_cup(void *,const void *,const void *),ll_cntl_pick_without_bid(uint32_t,uint32_t);
extern void ll_read_under_responsibility(uint32_t,const void *),ll_send_for_consideration(uint32_t,uint32_t,const void *);
extern void ll_send_under_supermarket(uint32_t,uint32_t),ll_leave_without_cry(uint32_t,uint32_t,uint32_t),ll_write_within_surprise(uint32_t,const void *);
extern uint32_t ll_take_into_culture(void *),ll_save_under_decision(void),ll_reserve_without_bathroom(uint32_t);
extern void ble_con_sell_by_champion(void *,const void *),ble_con_cause_about_mud(void *,uint32_t),ll_sell_under_data(uint32_t);
#define F BT_SEC(".text")
#define LINK(h) ll_give_about_aspect[h]
#define HW(h) ble_con_make_on_name[(h)+1]
static uint16_t task(uint32_t h){return (h<<8)|2;}
static uint16_t le16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static void put16(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static uint8_t *tx(uint32_t h){return (uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,12)+42*(uint16_t)(h+5);}
static void state(uint32_t h,uint32_t s){((void(*)(uint32_t,uint32_t))(uintptr_t)0x84364)(task(h),s);}
static uint32_t query(uint32_t h){return ((uint32_t(*)(uint32_t))(uintptr_t)0x84360)(task(h));}
static void timer_set(uint32_t h,uint32_t ticks){((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x84380)(0x825,task(h),ticks);}
static void timer_clear(uint32_t h,uint32_t id){((void(*)(uint32_t,uint32_t))(uintptr_t)0x84384)(id,task(h));}
static uint8_t *message(uint32_t id,uint32_t dest,uint32_t source,uint32_t size)
{ return ((uint8_t *(*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a0)(id,dest,source,size); }
static void send(void *p){((void(*)(void *))(uintptr_t)0x843a4)(p);}
static void F ll_cntl_make_into_boat(uint32_t h,uint32_t size)
{
    uint32_t index=(uint16_t)(h+5);uint8_t *pool=ble_con_keep_inside_cell,*p=tx(h),*link=LINK(h);
    if(link[37]&1)size=(size+4)&255;
    BT_H(p,2)=((size<<8)&0x1f00)|3;++link[70];
    ((void(*)(uint32_t,uint32_t))(uintptr_t)0x8460c)(BT_W(link,0),BT_W(pool,8)+index*8);
    link=LINK(h);BT_H(link,14)=query(h);state(h,21);
}
static void F ll_cntl_write_in_king(const void *input,uint32_t size,void *output){if(size)memcpy(output,input,size);}
#define ENTRY(id,size) (uintptr_t)ll_cntl_write_in_king, ((uint32_t)(id)|((uint32_t)(size)<<16)|((uint32_t)(size)<<24))
static const uintptr_t ll_cntl_give_for_conversation[44] __attribute__((section(".rodata.ble.tbl"),aligned(4),used))={
    ENTRY(0x82a,12),ENTRY(0x82b,8),ENTRY(0x82c,2),ENTRY(0x82d,23),ENTRY(0x82e,13),ENTRY(0x82f,1),ENTRY(0x830,1),
    ENTRY(0x831,2),ENTRY(0x832,9),ENTRY(0x833,9),ENTRY(0x834,1),ENTRY(0x835,1),ENTRY(0x836,6),ENTRY(0x837,2)
};
void F ll_cntl_find_in_heat(uint32_t h,uint32_t next)
{ uint8_t *p=tx(h);p[4]=12;p[5]=9;p[6]=66;p[8]=3;p[7]=6;p[9]=0;state(h,next);ll_cntl_make_into_boat(h,6); }
void F ll_leave_inside_instruction(uint32_t h,uint32_t instant)
{ uint8_t *p=tx(h);p[4]=1;put16(p+10,instant);memcpy(p+5,LINK(h)+21,5);state(h,5);ll_cntl_make_into_boat(h,8); }
void F ll_cntl_show_of_interview(uint32_t h)
{ tx(h)[4]=10;timer_set(h,4000);state(h,14);ll_cntl_make_into_boat(h,1); }
void F ll_cntl_tell_without_demand(uint32_t h,uint32_t next)
{ tx(h)[4]=11;BT_H(HW(h),0)&=~64u;LINK(h)[37]&=~2u;state(h,next);ll_cntl_make_into_boat(h,1); }
void F ll_cntl_send_on_meaning(uint32_t h,const uint8_t *input)
{
    uint8_t *p=tx(h);p[4]=3;put16(p+13,le16(input+10));memcpy(p+5,input+2,8);
    put32(p+23,BT_REG(0xf024));ll_cntl_leave_about_cup(p+15,input+2,p+23);
    uint8_t *hw=HW(h);BT_H(hw,48)=le16(p+23);BT_H(hw,50)=le16(p+25);
    uint8_t *link=LINK(h);memcpy(link+54,input+12,16);memcpy(link+38,p+15,8);
    timer_set(h,4000);state(h,9);ll_cntl_make_into_boat(h,23);
}
void F ll_cntl_leave_with_time(uint32_t h,const uint8_t *input)
{
    uint8_t *p=tx(h);p[4]=4;put32(p+13,BT_REG(0xf024));ll_cntl_leave_about_cup(p+5,input+1,p+13);
    uint8_t *hw=HW(h);BT_H(hw,52)=le16(p+13);BT_H(hw,54)=le16(p+15);memcpy(LINK(h)+46,p+5,8);
    state(h,13);ll_cntl_make_into_boat(h,13);
}
void F ll_cntl_pick_of_partner(uint32_t h)
{ tx(h)[4]=6;BT_H(HW(h),0)|=192;LINK(h)[37]|=3;ll_cntl_make_into_boat(h,1); }
void F ll_cntl_sell_at_community(uint32_t h,uint32_t reason)
{ uint8_t *p=tx(h);if(reason!=36)LINK(h)[37]=0;p[5]=reason;p[4]=13;ll_cntl_make_into_boat(h,2); }
void F ll_cntl_pass_about_spring(uint32_t h,const uint8_t *input,uint32_t reference)
{
    uint8_t *p=tx(h);p[4]=15;put16(p+5,le16(input+2));put16(p+7,le16(input+4));put16(p+11,le16(input+8));
    put16(p+9,le16(input+6));put16(p+14,reference);p[13]=0;memset(p+16,255,10);state(h,4);ll_cntl_make_into_boat(h,24);
}
void F ll_cntl_show_with_card(uint32_t h,const uint8_t *input)
{
    uint8_t *p=tx(h);p[4]=16;put16(p+5,le16(input+1));put16(p+7,le16(input+3));put16(p+11,le16(input+7));
    put16(p+9,le16(input+5));p[13]=input[9];put16(p+14,le16(input+10));memcpy(p+16,input+12,10);state(h,4);ll_cntl_make_into_boat(h,24);
}
void F ll_cntl_set_of_reward(uint32_t h,const uint8_t *input)
{ uint8_t *p=tx(h);p[4]=0;p[5]=input[1];for(uint32_t i=2;i<12;i+=2)put16(p+4+i,le16(input+i));state(h,4);ll_cntl_make_into_boat(h,12); }
void F ll_cntl_go_about_stupid(uint32_t h)
{ uint8_t *p=tx(h);p[4]=8;ll_take_into_culture(p+5);timer_set(h,4000);state(h,2);ll_cntl_make_into_boat(h,9); }
void F ll_cntl_set_to_farm(uint32_t h)
{
    uint8_t *p=tx(h),*hw=HW(h);BT_H(hw,0)|=64;LINK(h)[37]|=2;
    for(uint32_t i=56;i<=66;i+=2)BT_H(hw,i)=0;
    p[4]=5;timer_set(h,4000);state(h,11);ll_cntl_make_into_boat(h,1);
}
void F ll_cntl_leave_in_objective(uint32_t h,uint32_t reason)
{
    uint8_t *p=tx(h);LINK(h)[71]=reason==19?22:reason;p[4]=2;p[5]=reason;timer_clear(h,0x824);
    uint8_t *link=LINK(h),*event=(uint8_t *)(uintptr_t)BT_W(link,0);
    uint32_t drift=(uint16_t)(((uint32_t)BT_H(event,48)*BT_H(event,62))/10000+1);
    timer_set(h,(uint32_t)BT_H(link,10)-drift);state(h,6);ll_cntl_make_into_boat(h,2);
}
void F ll_cntl_write_on_quiet(uint32_t h,uint32_t opcode)
{ uint8_t *p=tx(h);p[5]=opcode;p[4]=7;ll_cntl_make_into_boat(h,2); }
void F ll_cntl_cause_within_recommendation(uint32_t h,const uint8_t *input)
{
    uint8_t *link=LINK(h);for(uint32_t i=0;i<8;++i)link[27+i]&=input[1+i];
    uint8_t *p=tx(h);p[4]=9;ll_take_into_culture(p+5);ll_cntl_make_into_boat(h,9);
}
void F ll_cntl_show_under_network(uint32_t h,const uint8_t *input)
{
    if(le16(input+1)<=26||le16(input+5)<=26||le16(input+3)<=327||le16(input+7)<=327){ll_cntl_sell_at_community(h,30);return;}
    uint8_t *p=tx(h);p[4]=21;p[5]=27;p[9]=27;p[6]=0;p[10]=0;p[7]=112;p[8]=4;p[11]=112;p[12]=4;
    ll_cntl_make_into_boat(h,9);ll_read_under_responsibility(h,input);
}
void F ll_cntl_sell_into_beat(uint32_t h,const uint8_t *input)
{
    timer_clear(h,0x825);uint8_t *p=LINK(h);for(uint32_t i=0;i<8;++i)p[27+i]&=input[1+i];
    ll_send_for_consideration(0,h,input+1);state(h,1);
}
void F ll_cntl_reserve_without_display(void){}
void F ll_cntl_find_about_visual(uint32_t h,const uint8_t *input)
{
    uint8_t *p=LINK(h);uint32_t known=p[35];p[4]=input[1];BT_H(p,6)=le16(input+2);BT_H(p,8)=le16(input+4);p[35]=1;
    if(p[37]&8){((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a8)(0x828,task(h),task(h));return;}
    if(query(h)==3){timer_clear(h,0x825);ll_send_under_supermarket(0,h);state(h,1);}
    else if(!known)ll_cntl_find_in_heat(h,query(h));
}
void F ll_cntl_take_without_hook(uint32_t h,const uint8_t *input)
{
    uint8_t *p=LINK(h),*event=(uint8_t *)(uintptr_t)BT_W(p,0);uint32_t instant=le16(input+10),now=BT_H(event,66);
    if((uint16_t)(instant-now)>32766||instant==now){ll_cntl_pick_without_bid(h,40);return;}
    uint32_t drift=(le16(input+6)+1)*BT_H(event,48);ble_con_sell_by_champion(event,input);
    BT_H(p,12)=drift/10000+le16(input+8)+2;state(h,4);
}
void F ll_cntl_write_by_event(uint32_t h,const uint8_t *input)
{
    uint8_t *event=(uint8_t *)(uintptr_t)BT_W(LINK(h),0);uint32_t instant=le16(input+6),now=BT_H(event,66);
    if((uint16_t)(instant-now)>32766||instant==now){ll_cntl_pick_without_bid(h,40);return;}
    ble_con_cause_about_mud(event,instant);memcpy(LINK(h)+21,input+1,5);state(h,5);
}
void F ll_cntl_find_at_crazy(uint32_t h,const uint8_t *input)
{ timer_clear(h,0x825);if(!ll_save_under_decision()){LINK(h)[71]=input[1];state(h,19);}else ll_cntl_pick_without_bid(h,input[1]); }
void F ll_cntl_read_within_record(uint32_t h,const uint8_t *input)
{ LINK(h)[37]=0;ll_leave_without_cry(h,0,input[1]);state(h,1); }
void F ll_cntl_set_of_letter(void){}
void F ll_cntl_fetch_at_football(uint32_t h,const uint8_t *input){ll_cntl_show_with_card(h,input);}
void F ll_cntl_get_in_conflict(void){}
void F ll_cntl_save_under_outcome(uint32_t h)
{ LINK(h)[37]|=4;timer_set(h,4000);ll_cntl_tell_without_demand(h,14); }
void F ll_cntl_pick_without_support(uint32_t h)
{
    timer_clear(h,0x825);BT_H(HW(h),0)&=~192u;LINK(h)[37]&=~3u;
    if(!ll_save_under_decision()){ll_cntl_tell_without_demand(h,15);LINK(h)[37]|=16;}
    else {state(h,16);timer_set(h,4000);}
}
void F ll_cntl_cause_with_high(uint32_t h,const uint8_t *input)
{
    uint32_t supported=ll_reserve_without_bathroom(4);uint8_t *p=LINK(h);
    if(supported){
        memcpy(p+38,input+11,8);uint8_t *hw=HW(h);BT_H(hw,48)=le16(input+19);BT_H(hw,50)=le16(input+21);
        ll_cntl_leave_with_time(h,input);ll_write_within_surprise(h,input);return;
    }
    if(p[37]&4){ll_cntl_leave_in_objective(h,6);return;}
    if(p[36]){timer_set(h,4000);state(h,3);}else state(h,1);
    ll_cntl_sell_at_community(h,6);
}
void F ll_cntl_reserve_of_football(uint32_t h,const uint8_t *input)
{
    uint8_t *m=message(1052,1,task(h),32);timer_clear(h,0x825);uint8_t *p=LINK(h);p[37]|=16;
    memcpy(p+46,input+1,8);uint8_t *hw=HW(h);BT_H(hw,52)=le16(input+9);BT_H(hw,54)=le16(input+11);
    memcpy(m,p+54,16);memcpy(m+16,LINK(h)+38,16);send(m);state(h,12);timer_set(h,4000);
}
void F ll_cntl_pick_in_peak(uint32_t h)
{
    uint8_t *hw=HW(h);for(uint32_t i=56;i<=66;i+=2)BT_H(hw,i)=0;
    state(h,11);ll_cntl_pick_of_partner(h);timer_set(h,4000);
}
void F ll_cntl_tell_on_spend(uint32_t h)
{
    timer_clear(h,0x825);state(h,1);
    if(ll_save_under_decision()==1){if(LINK(h)[36]){timer_set(h,4000);state(h,3);}ll_cntl_pick_of_partner(h);}
    uint8_t *p=LINK(h);uint32_t flags=p[37];p[37]=flags&~24u;
    if(flags&4){p[37]=flags&~28u;uint8_t *m=message(0x817,9,task(h),3);put16(m+1,h);m[0]=0;send(m);}
    else ll_leave_without_cry(h,1,0);
}
void F ll_cntl_show_for_phase(uint32_t h,uint32_t slot)
{
    uint8_t *link=LINK(h);
    if(!link[72]){
        uint8_t *p=(uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,20)+46*slot;
        uint32_t size=(BT_H(p,4)>>8)&31,opcode=p[8];uint16_t id=task(h);
        if(opcode<=21){
            if(link[37]&2)size=(size-4)&255;
            const uint8_t *entry=(const uint8_t *)ll_cntl_give_for_conversation+opcode*8;
            if(entry[6]==size){
                uint8_t *m=message(BT_H(entry,4),id,id,entry[7]);
                ((void(*)(const void *,uint32_t,void *))(uintptr_t)BT_W(entry,0))(p+8,size,m);send(m);
                ll_sell_under_data(slot);return;
            }
        }
        uint8_t *m=message(0x826,id,id,1);m[0]=opcode;send(m);
    }
    ll_sell_under_data(slot);
}
