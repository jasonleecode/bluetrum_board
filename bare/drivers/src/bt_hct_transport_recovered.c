/* Recovered libbtctrl.a:hct.o: HCI queues, credits, routing and ACL buffers. */
#include "bt_recovered_internal.h"
extern void bb_fetch_in_event(uint32_t),bb_get_without_card(uint32_t),bb_find_to_afternoon(uint32_t);
extern void hci_host_recv_packet(void *,uint32_t),ll_sell_under_data(uint32_t);
extern uint32_t bb_utils_keep_of_age(uint32_t),bb_utils_go_by_promise(uint32_t);
extern void *ext_util_read_without_raw(void *);
extern void hct_find_with_problem(void *),hct_get_on_birth(void *);
extern uint8_t *ble_con_keep_inside_cell;
uint8_t lm_proc_pass_to_knee[92] __attribute__((section(".btmem.bb.hcic"),aligned(4)))={0};
#define S lm_proc_pass_to_knee
#define B(n) BT_B(S,n)
#define H(n) BT_H(S,n)
#define W(n) BT_W(S,n)
#define P(n) ((uint8_t *)(uintptr_t)W(n))
#define F BT_SEC(".text")
static void F hct_make_at_sell(void);
static void F hct_take_of_metal(void);
static void F hct_pass_within_head(void);
#include "bt_hct_transport_tables.inc"
static uint8_t *message(uint32_t id,uint32_t dest,uint32_t source,uint32_t size)
{ return ((uint8_t *(*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a0)(id,dest,source,size); }
static void send(void *p) { ((void(*)(void *))(uintptr_t)0x843a4)(p); }
static void release(void *p) { ((void(*)(void *))(uintptr_t)0x84328)(p); }
static uint8_t *pop(void *p) { return ((uint8_t *(*)(void *))(uintptr_t)0x843d0)(p); }
static uint16_t opcode(void) { return ((uint32_t)B(24)<<10)|B(25); }
static void opcode_bytes(uint8_t *p) { uint16_t op=opcode();p[0]=op;p[1]=op>>8; }
static void F hct_make_at_sell(void)
{ uint8_t *p=message(0x107c,9,9,3);opcode_bytes(p);p[2]=1;send(p); }
static void F hct_take_of_metal(void)
{
    if(B(2)!=1)return;
    uint8_t *p=P(44),*data;uint32_t size,type;
    if(p){data=p+16+(int8_t)p[5];size=BT_H(p,6);type=4;}
    else {
        uint32_t flow=B(80)&253;
        if(flow && !H(68))return;
        size=H(76);
        if(H(64))H(66)=(H(66)&0x3fff)|0x1000;
        else {
            p=P(52);if(!p)return;
            H(66)=((uint32_t)p[24]<<12)|(BT_H(p,16)&0xfff);
            uint32_t length=BT_H(p,18);p=(uint8_t *)(uintptr_t)BT_W(p,20);
            if(!p)return;
            W(60)=(uintptr_t)p;H(64)=length;
        }
        if(flow==1){
            if(H(68))--H(68);
            uint32_t handle=H(66)&0xfff;
            uint32_t kind=bb_utils_keep_of_age(handle);
            uint32_t offset=(kind==4?74:70)+2*(handle&127);++H(offset);
        }
        if(size<H(64))H(64)-=size;else {size=H(64);H(64)=0;}
        p=P(60);W(60)=(uintptr_t)(p+size);
        p[-4]=H(66);p[-3]=H(66)>>8;p[-2]=size;p[-1]=size>>8;
        data=p-4;size=(uint16_t)(size+4);type=2;
    }
    B(0)=type;B(2)=0;bb_fetch_in_event(2);
    data[-1]=type;hci_host_recv_packet(data-1,size+1);
}
static void F hct_pass_within_head(void)
{
    ((void(*)(uint32_t))(uintptr_t)0x8434c)(3);B(2)=1;bb_get_without_card(2);
    if(B(0)==4)release(pop(S+44));
    else if(!H(64)){
        uint8_t *p=pop(S+52);
        if(bb_utils_keep_of_age(H(66)&0xfff)==4)ll_sell_under_data(p[25]);
        else ((void(*)(uint32_t))(uintptr_t)0x845a0)(BT_W(p,20));
        release(p);
    }
    hct_take_of_metal();
}
uint32_t F lm_proc_save_about_hearing(uint32_t size,uint32_t unused1,uint32_t credits,uint32_t unused2)
{
    (void)unused1;(void)unused2;if(!size)return 18;H(76)=size;
    if(!credits)return 18;H(68)=credits;return 0;
}
void F hct_set_to_method(uint32_t handle)
{
    if((B(80)&253)!=1)return;
    uint32_t kind=bb_utils_keep_of_age(handle),offset=(kind==4?74:70)+2*(handle&127);
    uint32_t saved=BT_REG(0x540);BT_REG(0x540)&=~1u;
    H(68)+=H(offset);uint32_t current=BT_REG(0x540);H(offset)=0;BT_REG(0x540)=current|(saved&1);
}
uint32_t F hct_leave_in_bother(uint32_t value)
{
    uint8_t features[8];ext_util_read_without_raw(features);
    switch(value&3){
    case 0:if(!(features[2]&1))return 17;break;
    case 1:if(!(features[1]&64))return 17;break;
    case 2:if(!(features[1]&128))return 17;break;
    }
    H(78)=value;return 0;
}
void *F hct_reserve_within_physics(uint32_t group,uint32_t command)
{
    if(group<=8){
        const uint8_t *entry=(const uint8_t *)hct_transport_table+group*8;
        uint32_t base=BT_W(entry,0);if(!base || command>=entry[4])return 0;
        return (void *)(uintptr_t)(base+command*4);
    }
    if(group==63 && command<=14)return (uint8_t *)hct_transport_table+80+command*4;
    return 0;
}
void *F hct_write_at_month(void)
{
    if(!B(26))return S+8;
    uint8_t *entry=P(28);uint32_t id=BT_H(entry,0),size=entry[3];
    if(size<B(27))size=B(27);
    return message(id,id>>10,9,size);
}
void F hct_set_by_god(void)
{
    if(!B(3))return;
    --B(3);
    if(B(26)){
        uint32_t id=BT_H(P(28),0);
        ((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a8)(id,id>>10,9);
    }else hct_make_at_sell();
}
void F hct_get_inside_minimum(void *payload)
{
    if(((uint32_t)B(24)<<10|B(25))!=0xc35){
        if(!B(3)){if(B(26))release((uint8_t *)payload-16);return;}
        --B(3);
    }
    if(!B(26))hct_make_at_sell();
    else if(P(28)[2])hct_find_with_problem(payload);
    else hct_get_on_birth(payload);
}
void F hct_tx_done(void) { bb_find_to_afternoon(3); }
void F hct_leave_with_ability(void *packet,uint32_t handle)
{
    uint8_t *p=packet;uint32_t old=p[10];BT_H(p,10)=bb_utils_go_by_promise(handle);
    if(old==5 && p[10]==2){
        switch(BT_H(p,8)){
        case 0x1400:BT_H(p,8)=0x800;break;
        case 0x1407:BT_H(p,8)=0x801;break;
        case 0x141a:BT_H(p,8)=0x803;break;
        case 0x1423:BT_H(p,8)=0x804;break;
        default:BT_H(p,10)=0;
        }
    }
    if(p[10])return;
    uint32_t id=BT_H(p,8);
    int short_event=id==0x1019 || (id>=0x1400 && id<=0x1408) ||
        (id>=0x140a && id<=0x1411) || id==0x1415 || (id>=0x141d && id<=0x141e);
    uint8_t *m=message(short_event?0x1454:0x1455,9,5,short_event?3:5);
    opcode_bytes(m);m[2]=2;
    if(!short_event){m[3]=handle;m[4]=handle>>8;}send(m);
}
uint32_t F hct_take_into_brain(void)
{
    uint32_t kind=B(41),capacity=679;uint8_t *p;
    if(B(40)<=3 && ((kind==1&&!H(42))||kind==3)){
        W(36)=(uintptr_t)(S+8);W(4)=(uintptr_t)(S+8);return capacity;
    }
    if(B(40)<=3 && kind==4){p=pop(ble_con_keep_inside_cell);capacity=27;}
    else p=((uint8_t *(*)(void))(uintptr_t)0x84598)();
    W(36)=(uintptr_t)p;
    if(!p){uint8_t *m=message(0x1467,9,5,1);m[0]=1;send(m);p=S+8;W(36)=(uintptr_t)p;}
    else if(kind==4 && B(40)<=3)p=(uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,12)+42*BT_H(p,4)+4;
    W(4)=(uintptr_t)p;return capacity;
}
void F hct_take_into_host(void)
{
    uint8_t *p;uint32_t size=H(34);
    if(B(40)>3 || B(41)==1){
        p=message(0x1426,H(42),9,12);BT_H(p,0)=H(32);p[8]=B(40);
        BT_H(p,2)=size>679?679:size;BT_W(p,4)=W(36);
    }else {
        p=message(0x80b,(uint16_t)((H(32)<<8)|2),9,12);BT_H(p,0)=H(32);p[2]=B(40);
        BT_H(p,4)=size>27?27:size;BT_W(p,8)=W(36);
    }
    send(p);
}
void F hct_give_in_discount(uint32_t handle)
{ uint32_t offset=70+2*(handle&127);uint32_t pending=H(offset);H(offset)=0;H(68)+=pending; }
void F lm_proc_tell_to_complex(const void *mask)
{ memcpy(S+84,mask,8);for(uint32_t i=0;i<8;++i)B(84+i)|=((const uint8_t *)hct_transport_table)[140+i]; }
void F bt21_go_about_pool(void)
{ memset(S+60,0,24);H(76)=0xffff;W(84)=hct_transport_table[37];W(88)=hct_transport_table[38]; }
void F bt21_save_under_dig(void)
{
    memset(S,0,92);
    ((void(*)(uint32_t,const void *))(uintptr_t)0x84368)(9,(const uint8_t *)hct_transport_table+156);
    ((void(*)(uint32_t,uint32_t))(uintptr_t)0x84364)(9,0);
    ((void(*)(void *))(uintptr_t)0x843c0)(S+44);((void(*)(void *))(uintptr_t)0x843c0)(S+52);
    B(2)=1;B(3)=5;W(4)=0;bt21_go_about_pool();
    ((void(*)(uint32_t,void (*)(void)))(uintptr_t)0x84340)(3,hct_pass_within_head);
}
void F hct_take_under_cook(void) { if(B(2)==1)hct_take_of_metal(); }
uint32_t F lm_proc_save_about_ruin(int32_t count,const void *handles,const void *credits)
{
    uint32_t result=0;
    for(int32_t i=0;i<count;++i){
        uint32_t handle=BT_H(handles,2*i),credit=BT_H(credits,2*i),kind=bb_utils_keep_of_age(handle);
        if((kind!=1&&kind!=4)||(B(80)&253)!=1){result=18;continue;}
        uint32_t offset=(kind==4?74:70)+2*(handle&127);
        if(H(offset)<credit){result=18;break;}
        H(68)+=credit;H(offset)-=credit;
    }
    hct_take_under_cook();return result;
}
void F hct_send_command(uint32_t op,uint32_t size,const void *payload)
{
    B(24)=op>>10;B(25)=op;B(27)=size;
    uint8_t *entry=hct_reserve_within_physics(op>>10,op&255);W(28)=(uintptr_t)entry;
    B(26)=entry&&BT_H(entry,0)?1:0;
    if(!size){hct_set_by_god();return;}
    uint8_t *p=hct_write_at_month();W(4)=(uintptr_t)p;
    uint32_t copied=size;if(p==S+8)copied=size>16?16:(size&255);
    memcpy(p,payload,copied);hct_get_inside_minimum(P(4));
}
static const char invalid_handle[] __attribute__((section(".rodata.str1.4"),aligned(4)))="handler %04x had disconnected\n";
static const char no_buffer[] __attribute__((section(".rodata.str1.4"),aligned(4)))="acl alloc error\n";
uint32_t F hct_acl_segment(uint32_t handle,uint32_t flags,uint32_t size,const void *payload)
{
    H(34)=size;B(40)=flags;H(32)=handle&0xfff;B(41)=bb_utils_keep_of_age(handle&0xfff);
    uint32_t connection=bb_utils_go_by_promise(H(32));H(42)=connection;
    if(B(41)==3||!connection){my_printf(invalid_handle,handle);return 0;}
    uint32_t capacity=hct_take_into_brain();uint8_t *p=P(4);
    if(p!=S+8){memcpy(p,payload,size>capacity?capacity:size);hct_take_into_host();}
    else my_printf(no_buffer);
    return 1;
}
