/* Recovered libbtctrl.a:ble_evt.o. Event lists, interval placement and ISR work. */
#include "bt_recovered_internal.h"
extern uint8_t *ble_con_make_on_name[];
extern void bthw_soft_kick(void),ble_evt_go_by_elevator(uint32_t,void *);
extern void ble_evt_cause_for_impress(uint32_t),ble_evt_find_without_school(void *),ble_evt_reserve_by_conflict(uint32_t,uint32_t);
uint8_t ble_con_show_of_beat[24] __attribute__((section(".btmem.bb.ble"),aligned(4)))={0};
#define E ble_con_show_of_beat
#define F BT_SEC(".text")
#define ISR BT_SEC(".com_text.bb.leisr")
#define PTR(p,n) ((uint8_t *)(uintptr_t)BT_W(p,n))
#define MASK UINT32_C(0x07ffffff)
#define WINDOW UINT32_C(0x30bb00)
#define ROM1(a,p) ((void(*)(void *))(uintptr_t)(a))(p)
static uint16_t le16(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8); }
static uint8_t *allocate(uint32_t size)
{ return ((uint8_t *(*)(uint32_t,uint32_t))(uintptr_t)0x84324)(size,1); }
static uint32_t uncertainty(uint32_t interval,uint32_t accuracy)
{ return ((uint32_t(*)(uint32_t,uint32_t))(uintptr_t)0x84630)(interval,accuracy); }
static void F ble_evt_cause_for_soil(uint8_t *p)
{ memset(p,0,84);ROM1(0x843c0,p+16);ROM1(0x843c0,p+24); }
BT_WEAK void ISR ble_evt_give_for_drawing(void)
{
    uint32_t saved=BT_REG(0x540)&1;BT_REG(0x540)&=~1u;
    if(((uint32_t(*)(uint32_t))(uintptr_t)0x84348)(6))bthw_soft_kick();
    BT_REG(0x540)=BT_REG(0x540)|saved;
}
void ISR ble_evt_show_under_paint(uint8_t *p)
{
    uint8_t *hw=ble_con_make_on_name[1+BT_H(p,64)];
    uint32_t value=BT_H(hw,10),hop=(value>>8)&31;
    BT_H(hw,10)=((value&63)+37-hop)%37|(hop<<8)|0xffff8000u;
}
void ISR ble_con_keep_by_success(uint8_t *p)
{
    if(p[73]==5&&!p[75]){
        uint8_t *hw=ble_con_make_on_name[1+BT_H(p,64)];
        if(!(BT_H(hw,76)&128)&&(BT_H(hw,12)&0x8000)){
            BT_H(hw,12)&=0x7fff;ble_evt_show_under_paint(p);
        }
    }
    ROM1(0x84658,p);
}
BT_WEAK void ISR ble_con_send_at_age(uint8_t *p,uint32_t value,uint32_t ready)
{
    if(p[76]==1){
        ((void(*)(void *,uint32_t,uint32_t))(uintptr_t)(ready?0x84650:0x84654))(p,value,ready);
        ble_evt_go_by_elevator(BT_H(p,64),p);
    }else if(p[76]==2){p[76]=0;ble_evt_cause_for_impress(BT_H(p,64));}
}
BT_WEAK void ISR ble_con_show_to_slip(uint8_t *p)
{
    if(!p[73])ble_evt_find_without_school(p);
    if(p[83]){ble_evt_reserve_by_conflict(BT_H(p,64),p[83]);p[83]=0;}
}
void F ble_con_fetch_at_pressure(uint32_t reset)
{
    if(reset){
        uint8_t *p=PTR(E,0),*q=PTR(E,8);
        while(p){uint8_t *next=PTR(p,0);ROM1(0x84328,p);p=next;}
        while(q){uint8_t *next=PTR(q,0);ROM1(0x84328,q);q=next;}
    }
    memset(E,0,24);ROM1(0x843c0,E);ROM1(0x843c0,E+8);E[20]=1;
    ((void(*)(uint32_t,void(*)(void)))(uintptr_t)0x84340)(6,ble_evt_give_for_drawing);
}
uint8_t *F ble_evt_sell_for_mortgage(uint32_t handle)
{
    uint8_t *p=PTR(E,0);while(p&&BT_H(p,64)!=handle)p=PTR(p,0);return p;
}
void F ble_con_keep_on_muscle(uint8_t *p,uint32_t handle)
{
    uint32_t interval=(uint16_t)((BT_H(ble_con_make_on_name[1+handle],20)+1)&~1u);
    p[73]=4;int32_t delta=(int32_t)BT_H(p,58)-(int32_t)interval;
    BT_H(p,64)=handle;BT_H(p,66)=0;BT_W(PTR(p,8),8)+=delta/2;BT_H(p,58)=interval;
    for(;;){uint32_t count=BT_H(p,70);BT_H(p,70)=count-1;if(!count)break;
        ROM1(0x84638,p);++BT_H(p,66);
    }
    if(p[74]){BT_H(0x11170,2*(BT_W(p,32)&15))=0;ROM1(0x8465c,p);}
}
uint32_t F ble_con_give_at_black(uint8_t *p)
{ uint16_t instant=7*BT_H(p,62)+BT_H(p,66);BT_H(p,68)=instant;p[76]=2;return (uint16_t)(instant+1); }
uint8_t *F ble_con_leave_of_dance(const uint8_t *parameters,const uint8_t *request,void *old,uint32_t handle)
{
    uint32_t accuracy=request[33]>>5,window=request[19],offset=BT_H(request,20);
    if(old)((void(*)(void *,uint32_t))(uintptr_t)0x84668)(old,0x2000000);
    uint8_t *p=allocate(84);ble_evt_cause_for_soil(p);BT_H(p,64)=handle;
    BT_H(p,60)=le16(parameters)*2;BT_H(p,62)=le16(parameters+2)+1;p[73]=5;BT_H(p,58)=0xffff;p[77]=accuracy;
    BT_H(p,48)=uncertainty(BT_H(p,60),accuracy);
    if(offset<=3){offset=(uint16_t)(offset+le16(parameters));ROM1(0x84638,p);BT_H(p,66)=1;}
    BT_W(p,36)=uncertainty((uint16_t)((window+offset+1)*2),accuracy);
    BT_H(p,50)=window*625+40;ROM1(0x8463c,p);
    uint8_t *hw=ble_con_make_on_name[2];uint32_t sub=BT_H(hw,72),time=BT_H(hw,68)|((uint32_t)BT_H(hw,70)<<16);
    uint16_t phase=936-sub;if(phase>624){phase=311-sub;++time;}
    BT_W(p,40)=((offset+1)*2+window+time)&MASK;BT_H(p,44)=phase;
    ((void(*)(void *,uint32_t))(uintptr_t)0x84644)(p,BT_W(p,40));
    BT_W(p,40)=(BT_W(p,40)-BT_H(p,60))&MASK;ROM1(0x84648,p);p[79]|=1;return p;
}
void F ble_con_give_within_suit(const uint8_t *parameters,uint8_t *old)
{
    uint8_t *p=allocate(84);ble_evt_cause_for_soil(p);BT_H(p,64)=BT_H(old,64);
    BT_H(p,60)=le16(parameters+4)*2;BT_H(p,62)=le16(parameters+6)+1;p[73]=5;BT_H(p,58)=0xffff;p[77]=old[77];
    BT_H(p,48)=uncertainty(BT_H(p,60),p[77]);
    BT_W(p,36)=uncertainty((uint16_t)((le16(parameters+2)+parameters[1])*2+BT_H(old,60)),p[77]);
    BT_H(p,50)=parameters[1]*625+40;p[79]|=1;BT_W(old,12)=(uintptr_t)p;
    BT_H(old,68)=le16(parameters+10)-1;old[76]=1;old[72]=parameters[1];BT_H(old,54)=le16(parameters+2);old[79]|=1;
}
void F ble_con_make_in_spirit(uint8_t *p,uint32_t instant)
{ p[76]=2;BT_H(p,68)=instant-1;p[79]|=1; }
BT_WEAK uint32_t ISR ble_evt_read_within_distribution(void)
{
    uint32_t saved=BT_REG(0x540)&1;BT_REG(0x540)&=~1u;BT_REG(0xf0fc)=1;
    while(BT_REG(0xf0fc)){}
    uint32_t now=BT_REG(0xf01c);BT_REG(0x540)=BT_REG(0x540)|saved;return now;
}
static uint32_t F ble_evt_give_without_worth(uint8_t *group,uint32_t offset)
{
    uint8_t *first=PTR(group,4);
    uint32_t time=BT_W(first,32)+2*(int32_t)(int16_t)(offset-BT_H(first,56));
    uint32_t now=ble_evt_read_within_distribution();
    if(((time-6-now)&MASK)>WINDOW)time+=BT_H(group,12);
    return time;
}
static void F ble_evt_pass_within_time(uint8_t *p,int32_t duration,uint32_t minimum,uint32_t maximum)
{
    uint8_t *group=PTR(E,8);uint32_t interval=0;
    while(group){
        if(BT_W(group,8)){
            uint32_t candidate=BT_H(group,12);
            for(uint32_t factor=1;factor<8;++factor){
                candidate=(uint16_t)(candidate*factor);
                if(candidate>=minimum&&candidate<=maximum){interval=candidate;break;}
            }
            if(interval&&interval!=65535)break;
        }
        interval=0;group=PTR(group,0);
    }
    if(!group){
        group=allocate(16);BT_W(group,0)=0;BT_W(group,4)=0;BT_W(group,8)=maximum>>1;BT_H(group,12)=maximum;
        uint32_t saved=BT_REG(0x540);BT_REG(0x540)&=~1u;
        uint8_t *next=PTR(E,8),*previous=0;
        while(next&&BT_H(next,12)>=maximum){previous=next;next=PTR(next,0);}
        if(previous)BT_W(previous,0)=(uintptr_t)group;else BT_W(E,8)=(uintptr_t)group;
        if(!next)BT_W(E,12)=(uintptr_t)group;
        BT_W(group,0)=(uintptr_t)next;BT_REG(0x540)=BT_REG(0x540)|(saved&1);interval=maximum;
    }
    BT_H(p,60)=interval;BT_W(p,8)=(uintptr_t)group;
    uint8_t *first=PTR(group,4);
    if(duration>=(int32_t)interval-5)duration=(uint16_t)(interval-6);
    uint32_t wanted=duration?(uint32_t)duration>>1:1;
    uint8_t *current=first,*previous=0,*best=0;
    uint32_t end=0,best_excess=65535,best_deficit=65535,found=0,gap;
    for(;;){
        uint32_t start=current?BT_H(current,56):BT_H(group,12)>>1;
        gap=(uint16_t)(start-end);
        if(gap>=wanted){
            int32_t excess=(int32_t)(gap-wanted);
            if(excess<(int32_t)best_excess){best_excess=(uint16_t)excess;best=previous;found=1;}
            if(gap==wanted)break;
        }else if(gap&&!found){
            int32_t deficit=(int32_t)(wanted-gap);
            if(deficit<(int32_t)best_deficit){best_deficit=(uint16_t)deficit;best=previous;}
        }
        if(!current)break;
        end=(uint16_t)(BT_H(current,56)+(BT_H(current,58)>>1));previous=current;current=PTR(current,4);
    }
    uint32_t length=(uint16_t)(2*(gap>=wanted?wanted:gap));BT_H(p,58)=length;BT_W(group,8)-=length>>1;
    if(best){
        BT_H(p,56)=BT_H(best,56)+(BT_H(best,58)>>1);
        BT_W(p,32)=ble_evt_give_without_worth(group,BT_H(p,56));BT_W(p,4)=BT_W(best,4);BT_W(best,4)=(uintptr_t)p;
    }else {
        BT_H(p,56)=0;
        uint32_t time=first?ble_evt_give_without_worth(group,0):(ble_evt_read_within_distribution()+4)&0x7fffffe;
        BT_W(p,32)=time;BT_W(p,4)=BT_W(group,4);BT_W(group,4)=(uintptr_t)p;
    }
}
uint8_t *F ble_con_show_without_uncle(uint32_t handle,int32_t duration,uint32_t minimum,uint32_t maximum,uint32_t latency)
{
    uint8_t *p=allocate(84);ble_evt_cause_for_soil(p);BT_H(p,64)=handle;BT_H(p,62)=latency+1;p[73]=3;
    ble_evt_pass_within_time(p,duration,minimum,maximum);return p;
}
uint8_t *F ble_con_give_to_series(uint8_t *old,int32_t duration,uint32_t minimum,uint32_t maximum,uint32_t latency,uint8_t *result)
{
    uint8_t *p=allocate(84);ble_evt_cause_for_soil(p);BT_H(p,62)=latency+1;p[73]=4;
    BT_H(p,64)=BT_H(old,64);BT_H(p,48)=BT_H(old,48);ble_evt_pass_within_time(p,duration,minimum,maximum);
    uint32_t events=(uint16_t)(BT_H(old,62)*7),target=(BT_W(old,32)+BT_H(old,60)*events)&MASK;
    uint32_t delta;
    while((delta=(BT_W(p,32)-target)&MASK)>WINDOW)BT_W(p,32)=(BT_W(p,32)+BT_H(p,60))&MASK;
    uint16_t instant=events+BT_H(old,66);BT_H(old,68)=instant-1;
    BT_H(result,0)=delta>>1;result[4]=1;BT_H(result,2)=instant;BT_W(old,12)=(uintptr_t)p;old[76]=1;return p;
}
uint8_t *F ble_con_cause_with_cold(uint32_t handle,uint32_t unused,uint32_t interval,uint32_t kind)
{
    (void)unused;uint8_t *p=allocate(84);ble_evt_cause_for_soil(p);BT_H(p,64)=handle;p[73]=kind;BT_H(p,60)=interval;
    BT_W(p,32)=(ble_evt_read_within_distribution()+6)&MASK;
    if(BT_REG(0xf020)<=311)BT_W(p,32)=(BT_W(p,32)+1)&MASK;
    BT_H(p,58)=1;return p;
}
uint32_t ISR ble_evt_find_for_stock(void)
{
    uint8_t *p=PTR(E,0);uint32_t now=ble_evt_read_within_distribution();
    if(!p||p[74])return 0;
    return ((BT_W(p,32)-8-(now&MASK))&MASK)>WINDOW;
}
