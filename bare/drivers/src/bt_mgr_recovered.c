/* Recovered libbtctrl.a:mgr.o: advertising/scanning, initiation and manager state.
 * Offset names retain the binary ABI; peripheral registers and ROM entrypoints
 * are those of the AB32VG1 controller, not a portable Bluetooth implementation. */
#include "bt_recovered_internal.h"
extern uint8_t *ble_con_keep_inside_cell;
extern uint8_t ble_con_show_of_beat[],mgr_read_for_boy[],mgr_take_inside_band[];
extern uint8_t mgr_find_within_welcome[],mgr_set_into_theory[],mgr_keep_under_combine[];
extern uint32_t ll_util_give_to_credit(void *),ll_util_fetch_to_staff(uint32_t,const void *);
extern uint32_t mgr_take_on_diamond(const void *),mgr_write_in_difference(const void *,uint32_t,void *);
extern uint32_t ble_con_do_without_notice(const void *,uint32_t),mgr_fetch_on_savings(void),mgr_find_at_writing(const void *,const void *);
extern void mgr_make_within_entrance(uint32_t),mgr_write_at_other(void),ble_get_local_bd_addr(void *);
extern void *ble_con_take_inside_pace(void *,const void *,void *,uint32_t);
extern void *ble_con_make_on_street(void *,void *,void *,uint32_t),*ble_con_get_of_shoe(void *,void *);
extern void *ble_con_reserve_on_option(void *,uint32_t),*ble_con_do_by_wall(uint32_t),*ble_con_cause_within_youth(void *,void *,uint32_t);
extern void ll_do_under_regret(void *,void *),ble_con_find_of_blow(void *,uint32_t);
extern void ble_con_find_inside_official(void *),ble_con_write_under_state(void *);
extern void bb_fetch_in_event(uint32_t),bb_get_without_card(uint32_t);
#define F BT_SEC(".text")
#define ISR BT_SEC(".com_text.bb.leisr")
static uint8_t manager[156] __attribute__((section(".btmem.bb.mgr"),aligned(4),used));
__asm__(".global mgr_cause_within_nobody\n.pushsection .btmem.bb.mgr,\"aw\",@progbits\n.balign 4\nmgr_cause_within_nobody:\n.popsection\n.type mgr_cause_within_nobody,@object\n.size mgr_cause_within_nobody,148\n"
        ".global mgr_make_about_rise\n.set mgr_make_about_rise,manager+148\n.type mgr_make_about_rise,@object\n.size mgr_make_about_rise,8\n");
#define M manager
static uint8_t filter[10] __attribute__((section(".sbss"),aligned(4)));
static uint8_t mgr_show_with_parent[7] __attribute__((section(".sdata"),aligned(4)))={0xf0,0xaa,0,0xff,0,0x0f,0x55};
static const uintptr_t mgr_leave_about_strength[5] __attribute__((section(".com_rodata.ble.tbl"),aligned(4),used))={
 0,(uintptr_t)mgr_find_within_welcome,(uintptr_t)mgr_set_into_theory,(uintptr_t)mgr_keep_under_combine,0x10006};
static const uint8_t tables[88] __attribute__((section(".rodata.ble.tbl"),aligned(4),used))={
 255,255,255,255,255,255,0,0,255,0,60,31,0,0,0,0,1,0,0,0,0,0,0,0,
 32,0,128,0,0,192,0,0,0,0,132,0,0,0,168,34,0,0,0,0,0,0,0,0,0,247,255,255,127};
__asm__(".global mgr_pick_into_politics\n.set mgr_pick_into_politics,tables+8\n.type mgr_pick_into_politics,@object\n.size mgr_pick_into_politics,8\n"
        ".global mgr_take_with_try\n.set mgr_take_with_try,tables+16\n.type mgr_take_with_try,@object\n.size mgr_take_with_try,8\n"
        ".global mgr_sell_within_eat\n.set mgr_sell_within_eat,tables+24\n.type mgr_sell_within_eat,@object\n.size mgr_sell_within_eat,64\n");
static uint16_t le16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static uint8_t *tx(void){return (void *)(uintptr_t)BT_W(ble_con_keep_inside_cell,12);}
static void *descriptor(uint32_t offset){return (void *)(uintptr_t)(BT_W(ble_con_keep_inside_cell,8)+offset);}
static void state(uint32_t value){((void(*)(uint32_t,uint32_t))(uintptr_t)0x84364)(1,value);}
static void *allocate(uint32_t id,uint32_t dest,uint32_t source,uint32_t size)
{return ((void *(*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a0)(id,dest,source,size);}
static void send(void *p){((void(*)(void *))(uintptr_t)0x843a4)(p);}
static void release(void *p){((void(*)(void *))(uintptr_t)0x84328)(p);}
static void *pop(void *p){return ((void *(*)(void *))(uintptr_t)0x843d0)(p);}
static uint32_t random32(void){return ((uint32_t(*)(void))(uintptr_t)0x84594)();}
static void F mgr_go_for_frame(uint32_t index,const uint8_t *address)
{for(uint32_t i=0;i<6;i+=2)BT_H(0x11658+6*index,i)=le16(address+i);}
static void F mgr_make_about_pick(uint32_t index,const uint8_t *address)
{for(uint32_t i=0;i<6;i+=2)BT_H(0x1166a+6*index,i)=le16(address+i);}
void F mgr_make_to_delivery(const void *address,uint32_t enabled)
{filter[0]=enabled;if(enabled)memcpy(filter+4,address,6);}
void F mgr_save_under_green(uint8_t *packet)
{
 uint32_t timeout=BT_H(packet,34),interval=BT_H(packet,30),latency=BT_H(packet,32);
 if((uint16_t)(timeout-10)>3190||(uint16_t)(interval-6)>3194||latency>500||timeout*10<(interval*5/2)*(latency+1))return;
 if(filter[0]&&memcmp(filter+4,packet+8,6))return;
 uint16_t handle;uint8_t parameters[16];if(ll_util_give_to_credit(&handle))return;
 M[44]=1;++M[58];BT_H(parameters,0)=BT_H(packet,30);BT_H(parameters,2)=BT_H(packet,32);BT_H(parameters,4)=BT_H(packet,34);
 parameters[13]=packet[41]&31;parameters[14]=packet[41]>>5;memcpy(parameters+6,packet+8,6);parameters[12]=(BT_H(packet,4)>>6)&1;
 state(BT_W(M,48)?4:0);void *event=ble_con_take_inside_pace(parameters,packet+8,(void *)(uintptr_t)BT_W(M,48),handle);
 BT_W(M,48)=0;ll_do_under_regret(parameters,event);ll_util_fetch_to_staff(handle,packet+36);
}
void F mgr_get_at_coffee(uint8_t *packet)
{
 uint8_t parameters[16];M[44]=0;++M[58];uint8_t *p=tx();
 BT_H(parameters,0)=BT_H(p,266);BT_H(parameters,2)=BT_H(p,268);BT_H(parameters,4)=BT_H(p,270);
 parameters[13]=p[277]&31;parameters[14]=p[277]>>5;parameters[12]=(packet[4]>>6)&1;memcpy(parameters+6,packet+8,6);
 if(BT_W(M,48)){ble_con_find_of_blow((void *)(uintptr_t)BT_W(M,48),BT_H(M,46));ll_do_under_regret(parameters,(void *)(uintptr_t)BT_W(M,48));BT_W(M,48)=0;}
 state(0);
}
void F mgr_show_within_nobody(uint32_t id,uint32_t dest,uint32_t source,uint32_t status)
{uint8_t *p=allocate(id,dest,source,1);p[0]=status;send(p);}
void F ll_proc_go_for_department(uint32_t id,uint32_t dest,uint32_t source,uint32_t status)
{uint8_t *p=allocate(id,dest,source,1);p[0]=status;send(p);}
void F mgr_do_into_item(void){mgr_show_within_nobody(1058,9,1,0);}
static void test_pattern(uint8_t *buffer,uint32_t length,uint32_t pattern)
{
 if(pattern<=7&&((1u<<pattern)&246)){memset(buffer,mgr_show_with_parent[pattern-1],length);BT_REG(0xf0e0)&=~0x1000u;}
 else if(pattern<=7&&((1u<<pattern)&9)){BT_REG(0xf0e0)=(BT_REG(0xf0e0)&~0x2000u)|((pattern&1)<<13);BT_REG(0xf0e0)|=0x1000;}
}
uint32_t F mgr_leave_by_roll(const uint8_t *p)
{
 if(p[0]>39||(uint8_t)(p[1]-1)>36||p[2]>7)return 18;
 uint8_t *buffer=tx();M[55]=0;state(5);BT_REG(0xf000)|=0x40000;
 test_pattern(buffer+340,p[1],p[2]);BT_H(buffer,338)=((p[1]<<8)&0x3f00)|(p[2]&15);
 BT_W(M,48)=(uintptr_t)ble_con_reserve_on_option(descriptor(64),p[0]);return 0;
}
uint32_t F mgr_keep_by_commission(const uint8_t *p)
{if(p[0]>39)return 18;M[55]=1;BT_H(M,56)=0;BT_REG(0xf000)|=0x40000;state(5);BT_W(M,48)=(uintptr_t)ble_con_do_by_wall(p[0]);return 0;}
uint32_t F mgr_get_to_town(const uint8_t *p)
{
 uint32_t minimum=le16(p),maximum=le16(p+2);uint16_t index;
 if(maximum<minimum||p[6]>1||p[5]>1||p[13]>7||p[14]>3)return 18;
 if(minimum<=(p[4]<=1?31u:29u)||maximum>0x4000)return 48;
 M[18]=7;
 if(p[4]==1){M[18]=1;BT_H(tx(),254)=0xc01;if(!mgr_take_on_diamond(p+7)){
   if(!p[14]||mgr_write_in_difference(p+7,p[6],&index)){BT_H(tx(),254)|=(p[6]<<7)&128;memcpy(tx()+256,p+7,6);}
 }}else if(p[4]==0||p[4]==2||p[4]==3){M[18]=p[4]==0?0:(p[4]==2?6:2);M[17]=p[14];BT_H(M,14)=maximum;BT_H(M,12)=minimum;}
 M[21]=p[5];if(!ble_con_do_without_notice(p+13,1))return 18;M[16]=p[13];return 0;
}
uint32_t F mgr_do_without_strength(const uint8_t *p)
{
 if(!p[0]){ble_con_find_inside_official((void *)(uintptr_t)BT_W(M,48));BT_W(M,48)=0;state(4);return 0;}
 if(M[44]!=2&&M[18]<=1)return 12;
 mgr_make_within_entrance(M[21]);uint32_t kind=M[18];void *event;
 if(kind==1)event=ble_con_make_on_street(M+8,descriptor(48),0,0);
 else if(kind==0||kind==2||kind==6){BT_H(tx(),338)=((M[19]+6)<<8&0x3f00)|kind;void *response=0;
  if(kind!=2){BT_H(tx(),296)=((M[20]+6)<<8&0x3f00)|4;response=descriptor(56);}
  event=ble_con_make_on_street(M+8,descriptor(64),response,0);
 }else return 18;
 BT_W(M,48)=(uintptr_t)event;state(1);return 0;
}
BT_WEAK uint32_t ISR mgr_show_in_wife(const uint8_t *p)
{uint32_t size=p[0];if(size>31)return 18;M[19]=size;if(size)memcpy(tx()+340,p+1,size);BT_H(tx(),338)=((size+6)<<8&0x3f00)|(BT_H(tx(),338)&0xc0ff);return 0;}
BT_WEAK void ISR ble_con_do_by_fill(void)
{if(BT_W(M,8)){mgr_show_in_wife((uint8_t *)(uintptr_t)BT_W(M,8)+16);release((void *)(uintptr_t)BT_W(M,8));BT_W(M,8)=0;}}
uint32_t F mgr_send_without_alternative(const uint8_t *p)
{uint32_t size=p[0];if(size>31)return 18;M[20]=size;if(size)memcpy(tx()+298,p+1,size);return 0;}
uint32_t F mgr_leave_on_night(const uint8_t *p)
{
 uint32_t interval=le16(p+1),window=le16(p+3);
 if(interval<window||p[0]>1||(uint16_t)(window-4)>16380||(uint16_t)(interval-4)>16380)return 18;
 M[28]=p[6];BT_H(M,24)=interval;BT_H(M,26)=window;M[29]=p[0];M[31]=p[5];BT_H(tx(),254)=p[0]==1?0xc03:0;return 0;
}
uint32_t F mgr_send_for_rate(const uint8_t *p)
{
 if(!p[0]){ble_con_write_under_state((void *)(uintptr_t)BT_W(M,48));BT_W(M,48)=0;state(4);mgr_write_at_other();return 0;}
 M[30]=p[1];mgr_make_within_entrance(M[31]);
 if(!M[29]){BT_H(tx(),254)=0;BT_W(M,48)=(uintptr_t)ble_con_get_of_shoe(M+24,0);}
 else if(M[29]==1){BT_H(tx(),254)=0xc03;BT_W(M,48)=(uintptr_t)ble_con_get_of_shoe(M+24,descriptor(48));}
 state(2);return 0;
}
void F mgr_set_inside_one(void)
{for(uint32_t i=0;i<3;i++)mgr_make_about_pick(i,tables);for(uint32_t i=0;i<3;i++)mgr_go_for_frame(i,tables);}
void F mgr_write_inside_pay(void);
void F ble_take_for_screw(uint32_t reset)
{
 ((void(*)(uint32_t,const void *))(uintptr_t)0x84368)(1,mgr_leave_about_strength);state(0);
 ((void(*)(uint32_t,void(*)(void)))(uintptr_t)0x84340)(2,mgr_write_inside_pay);
 M[55]=3;M[54]=0;BT_H(M,56)=0;M[40]=random32();M[41]=0;M[42]=0;memset(M+59,255,4);M[63]=31;BT_W(M,48)=0;mgr_set_inside_one();
 M[32]=31;M[76]=1;M[43]=1;memset(M+33,0,7);M[44]=2;BT_H(M,24)=16;BT_H(M,26)=8;M[52]=0;M[28]=0;M[29]=0;M[30]=0;mgr_write_at_other();
 BT_H(M,14)=160;BT_H(M,12)=160;M[17]=0;M[18]=0;M[16]=7;M[19]=0;M[20]=0;M[21]=0;
 BT_H(tx(),338)=1536;BT_H(tx(),296)=1540;BT_W(M,64)=0;BT_H(M,68)=0;BT_W(M,8)=0;M[58]=0;
 uint8_t address[8];ble_get_local_bd_addr(address);
 if(mgr_fetch_on_savings()==1)address[5]|=0xc0;
 else if(mgr_fetch_on_savings()==2)address[5]&=63;
 else if(mgr_fetch_on_savings()==3)address[5]=(address[5]&63)|64;
 uint32_t partial=mgr_fetch_on_savings()==3,ones=0,total=partial?22:46;
 for(uint32_t bit=0;bit<total;bit++)ones+=(address[(partial?3:0)+bit/8]>>(bit%8))&1;
 if(!ones||ones==total)address[partial?3:0]=85;
 BT_W(M,64)=BT_W(address,0);BT_H(M,68)=BT_H(address,4);
 uint32_t value=BT_REG(0xf024);for(uint32_t i=0;i<4;i++)M[70+i]=value>>(8*i);value=BT_REG(0xf028);M[74]=value;M[75]=value>>8;
 if(reset){void *node;while((node=pop(M)))release(node);}
 ((void(*)(void *))(uintptr_t)0x843c0)(M);
}
uint32_t F mgr_cause_in_go(const uint8_t *p)
{
 uint16_t index;if(mgr_write_in_difference(p+1,p[0],&index))return 18;
 if(!mgr_write_in_difference(tables,p[0],&index))return 7;
 if(!p[0])mgr_go_for_frame(index,p+1);else if(p[0]==1)mgr_make_about_pick(index,p+1);else return 18;return 0;
}
uint32_t F mgr_write_by_being(const uint8_t *p)
{uint16_t index;if(!mgr_write_in_difference(p+1,p[0],&index))return 18;if(p[0]==1)mgr_make_about_pick(index,tables);else mgr_go_for_frame(index,tables);return 0;}
uint32_t F mgr_pass_inside_top(uint8_t *p)
{
 if(M[44]==1)return 12;
 if(ll_util_give_to_credit(M+46))return 9;
 uint32_t maximum=le16(p+15),latency=le16(p+17),timeout=le16(p+19);
 if(le16(p)<le16(p+2)||maximum<le16(p+13)||le16(p+23)<le16(p+21)||le16(p+13)<6||maximum>3200||
   (uint16_t)(timeout-10)>3190||latency>500||timeout*10<(maximum*5/2)*(latency+1))return 18;
 uint32_t first=random32(),second=random32();uint8_t *buffer=tx();BT_H(buffer,254)=(p[5]<<7)|0x2205;
 BT_H(buffer,260)=first;buffer[262]=first>>16;uint32_t prefix=mgr_read_for_boy[M[42]]<<4;
 buffer[256]=(M[40]&15)|prefix;buffer[257]=(M[40]>>4)|prefix;buffer[258]=(second&15)|prefix;buffer[259]=((uint8_t)second>>4)|(mgr_take_inside_band[M[41]]<<4);
 M[41]=(M[41]+1)%3;++M[40];BT_H(buffer,268)=latency;BT_H(buffer,270)=timeout;memcpy(buffer+272,M+59,5);
 uint32_t hop=first&15;if(hop<=4)hop+=5;buffer[277]=hop|(ble_con_show_of_beat[20]<<5);
 mgr_make_within_entrance(p[12]);state(3);BT_W(M,48)=(uintptr_t)ble_con_cause_within_youth(p,descriptor(48),BT_H(M,46));return 0;
}
void F mgr_read_into_shock(uint8_t *out,const uint8_t *packet)
{
 out[0]=2;out[1]=1;out[42]=BT_H(packet,6);uint32_t kind=BT_H(packet,4)&15;
 switch(kind){case 0:out[2]=0;break;case 1:out[2]=1;break;case 2:out[2]=3;break;case 4:out[2]=4;break;case 6:out[2]=2;break;default:out[2]=kind;break;}
 if(out[2]==1)out[10]=0;else {out[10]=((BT_H(packet,4)>>8)&63)-6;memcpy(out+11,packet+14,out[10]);}
 memcpy(out+4,packet+8,6);out[3]=(BT_H(packet,4)>>6)&1;
 if(out[10]<=30){out[11+out[10]]=out[42];BT_H(out,-2)=out[10]+12;}
}
void F mgr_pick_of_answer(const uint8_t *packet)
{
 uint8_t address[6];memcpy(address,packet+8,6);uint32_t kind=BT_H(packet,4)&15,eligible=1;
 if(M[30]==1||kind==1){
  for(uint32_t i=0;i<10;i++){uint8_t *record=M+77+7*i;
   if(!record[0]){memcpy(record+1,address,6);record[0]=1u<<kind;break;}
   if(mgr_find_at_writing(address,record+1)){if(record[0]&(1u<<kind))eligible=0;else record[0]|=1u<<kind;break;}
  }
 }
 if((M[32]&2)&&eligible){uint8_t *p=allocate(1095,9,1,43);mgr_read_into_shock(p,packet);send(p);}
}
void F mgr_make_by_hand(const uint8_t *p)
{
 for(uint32_t i=0;i<4;i++){uint32_t value=(uint32_t)le16(p+4*i)|((uint32_t)le16(p+4*i+2)<<16);BT_REG(0xf0c4+4*i)=value;}
 memcpy((void *)0x11334,p+16,16);BT_REG(0xf0d4)=820;BT_REG(0xf0c0)=1;bb_fetch_in_event(16);M[52]=1;
}
void F mgr_write_inside_pay(void)
{
 uint8_t *node=pop(M);((void(*)(uint32_t))(uintptr_t)0x8434c)(2);if(!node)return;
 uint8_t *p=allocate(1087,BT_H(node,12),1,17);
 if(node[12]==2)memcpy(p+1,(void *)0x11344,16);
 else {uint8_t data[16];memcpy(data,(void *)0x11344,16);for(uint32_t i=0;i<16;i++)p[1+i]=data[15-i];}
 p[0]=0;send(p);release(node);if(BT_W(M,0))mgr_make_by_hand((uint8_t *)(uintptr_t)BT_W(M,0)+16);
 else {bb_get_without_card(16);M[52]=0;}
}
void ISR ble_evt_find_without_school(void){BT_W(M,48)=0;}
BT_WEAK void F mgr_leave_inside_minute(const uint8_t *p)
{
 BT_REG(0xf000)|=0x40000;test_pattern((void *)0x114a8,p[1],p[2]);BT_H(0x114a4,0)=0;BT_H(0x114a6,0)=((p[1]<<8)&0x3f00)|(p[2]&15);
 uintptr_t hw=0x112e6;BT_H(hw,16)=1188;BT_H(hw,0)=28;BT_H(hw,6)=0x5555;BT_B(hw,8)=0x55;BT_H(hw,14)=0x8001;
 BT_H(hw,2)=0x4129;BT_H(hw,22)=2400;BT_H(hw,4)=0x7176;BT_B(hw,9)=0;BT_H(hw,12)=1024;BT_H(hw,30)=0;BT_H(hw,24)=0;BT_H(hw,26)=0;BT_H(hw,28)=0;
 uint32_t channel=p[0],mapped=channel==0?37:(channel==12?38:(channel==39?39:(uint8_t)(channel-(channel<=11?1:2))));BT_H(hw,10)=0x8000|mapped;
 for(uint32_t i=0;i<8;i++)BT_H(0x11170+4*i,0)=742;
}
