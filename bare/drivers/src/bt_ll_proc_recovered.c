/* Recovered libbtctrl.a:ll_proc.o: link task message handlers and state guards. */
#include "bt_recovered_internal.h"
extern uint8_t *ll_give_about_aspect[],*ble_con_make_on_name[],*ble_con_keep_inside_cell;
extern uint32_t ll_save_under_decision(void),bb_rf_fetch_within_fix(uint32_t,uint32_t),bb_rf_set_on_inflation(uint32_t,uint32_t);
extern void ll_sell_under_data(uint32_t),ll_tell_under_position(uint32_t),ll_go_into_variation(uint32_t),ll_read_of_yellow(void *);
extern void ll_get_into_contribution(uint32_t,uint32_t),ble_evt_reserve_by_conflict(uint32_t,uint32_t);
extern void ll_tell_under_slip(uint32_t,uint32_t,uint32_t,uint32_t),ll_leave_without_cry(uint32_t,uint32_t,uint32_t);
extern void ll_send_for_consideration(uint32_t,uint32_t,void *),ll_send_under_supermarket(uint32_t,uint32_t);
extern void ll_make_to_display(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t),ll_reserve_within_drawing(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
extern void ll_proc_go_for_department(uint32_t,uint32_t,uint32_t,uint32_t),ble_con_find_inside_service(void *,void *,void *);
extern void ll_cntl_pick_without_bid(uint32_t,uint32_t),ll_cntl_write_on_quiet(uint32_t,uint32_t),ll_cntl_find_in_heat(uint32_t,uint32_t);
extern void ll_cntl_leave_in_objective(uint32_t,uint32_t),ll_cntl_sell_at_community(uint32_t,uint32_t),ll_cntl_show_for_phase(uint32_t,uint32_t);
extern void ll_cntl_pick_without_support(uint32_t),ll_cntl_save_under_outcome(uint32_t),ll_cntl_tell_on_spend(uint32_t),ll_cntl_pick_in_peak(uint32_t);
extern void ll_cntl_show_of_interview(uint32_t),ll_cntl_go_about_stupid(uint32_t),ll_cntl_set_to_farm(uint32_t);
extern void ll_cntl_read_within_record(uint32_t,void *),ll_cntl_find_about_visual(uint32_t,void *),ll_cntl_sell_into_beat(uint32_t,void *);
extern void ll_cntl_cause_within_recommendation(uint32_t,void *),ll_cntl_reserve_of_football(uint32_t,void *),ll_cntl_cause_with_high(uint32_t,void *);
extern void ll_cntl_find_at_crazy(uint32_t,void *),ll_cntl_write_by_event(uint32_t,void *),ll_cntl_take_without_hook(uint32_t,void *);
extern void ll_cntl_send_on_meaning(uint32_t,void *),ll_cntl_set_of_reward(uint32_t,void *);
#define HANDLER(name) static uint32_t BT_SEC(".text") name( \
 uint32_t id __attribute__((unused)),uint8_t *p __attribute__((unused)), \
 uint32_t dest __attribute__((unused)),uint32_t source __attribute__((unused)))
static uint16_t le16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static void put16(uint8_t *p,uint32_t value){p[0]=value;p[1]=value>>8;}
static uint32_t query(uint32_t task){return ((uint32_t(*)(uint32_t))(uintptr_t)0x84360)(task);}
static void state(uint32_t task,uint32_t value){((void(*)(uint32_t,uint32_t))(uintptr_t)0x84364)(task,value);}
static uint8_t *allocate(uint32_t id,uint32_t dest,uint32_t source,uint32_t size)
{return ((void *(*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a0)(id,dest,source,size);}
static void send(void *p){((void(*)(void *))(uintptr_t)0x843a4)(p);}
static void timer(uint32_t id,uint32_t task,uint32_t ticks){((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x84380)(id,task,ticks);}
static void clear_timer(uint32_t id,uint32_t task){((void(*)(uint32_t,uint32_t))(uintptr_t)0x84384)(id,task);}
static void empty(uint32_t id,uint32_t dest,uint32_t source){((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x843a8)(id,dest,source);}
static uint32_t inactive(uint32_t s){return s==0||s==19||s==20;}
static void reject_encrypted(uint32_t h){if(ll_give_about_aspect[h][37]&16)ll_cntl_pick_without_bid(h,61);}
static void encryption_failure(uint32_t h,uint32_t reason)
{
 if(ll_give_about_aspect[h][37]&4){uint8_t *m=allocate(0x817,9,(h<<8)|2,3);m[1]=h;m[2]=0;m[0]=reason;send(m);}
 else ll_leave_without_cry(h,0,reason);
}
HANDLER(ll_proc_show_into_sing)
{
 uint8_t *m=allocate(0x810,source,dest,4);uint32_t s=query(dest);
 if(inactive(s)){m[0]=12;m[3]=0;}else {m[0]=0;m[3]=ll_give_about_aspect[le16(p)][26];}
 put16(m+1,le16(p));send(m);return 0;
}
HANDLER(ll_proc_give_for_satisfaction)
{
 uint32_t s=query(dest);
 if(inactive(s)){
  uint32_t irq=BT_REG(0x540);BT_REG(0x540)&=~1u;
  ((void(*)(void *,void *))(uintptr_t)0x843c8)(ble_con_keep_inside_cell,(void *)(uintptr_t)BT_W(p,8));
  BT_REG(0x540)=BT_REG(0x540)|(irq&1);ble_evt_reserve_by_conflict(BT_H(p,0),1);
 }else {
  uint8_t *link=ll_give_about_aspect[BT_H(p,0)];if(link[37]&8)return 2;
  uint32_t kind=(p[2]&3)?1:2,length=p[4];if(link[37]&1)length=(uint8_t)(length+4);
  uint8_t *node=(void *)(uintptr_t)BT_W(p,8),*buffer=(void *)(uintptr_t)BT_W(ble_con_keep_inside_cell,12);
  BT_H(buffer+42*BT_H(node,4),2)=((length<<8)&0x1f00)|kind;++link[70];
  ((void(*)(void *,void *))(uintptr_t)0x8460c)((void *)(uintptr_t)BT_W(link,0),node);
 }
 return 0;
}
HANDLER(ll_proc_reserve_of_property)
{uint32_t h=dest>>8;state(dest,ll_give_about_aspect[h][14]);if(query(dest)==6)ll_cntl_pick_without_bid(h,ll_give_about_aspect[h][71]);return 0;}
HANDLER(ll_proc_send_in_fuel)
{
 uint32_t h=dest>>8,s=query(dest);if(s==19)ll_cntl_pick_without_bid(h,34);
 else if(s&&s!=20){uint8_t *link=ll_give_about_aspect[h];ll_cntl_pick_without_bid(h,link[73]&&(link[77]&136)!=136?62:8);}
 return 0;
}
HANDLER(ll_proc_go_on_event)
{
 uint32_t h=dest>>8,count=p[1],slot=p[0];uint8_t *link=ll_give_about_aspect[h];link[70]-=p[2]+p[3];
 if(p[2])ble_evt_reserve_by_conflict(h,p[2]);
 if(p[3])empty(0x827,dest,dest);
 uint32_t s=query(dest);if(s==7&&!ll_give_about_aspect[h][70])state(dest,8);
 else if(s==19)ll_cntl_pick_without_bid(h,ll_give_about_aspect[h][71]);
 uint32_t recorded=0,stopped=0;
 while(count){
  if(stopped)ll_sell_under_data(slot);
  else {
   uint8_t *packet=(uint8_t *)(uintptr_t)BT_W(ble_con_keep_inside_cell,20)+46*slot;link=ll_give_about_aspect[h];
   uint32_t flags=BT_H(packet,2),signal=BT_H(packet,6);
   link[26]=bb_rf_fetch_within_fix((signal&255)>>2,(signal&3)|((flags>>8)&~3u));flags=BT_H(packet,2);
   if(flags&31){if(((flags&1)&&!recorded)||(flags&254)){ll_give_about_aspect[h][77]=flags;recorded=1;}
    if(flags&16){ll_cntl_pick_without_bid(h,61);stopped=1;}ll_sell_under_data(slot);
   }else {
    link=ll_give_about_aspect[h];link[73]=0;if(link[74])link[74]=0;else timer(0x824,dest,BT_H(link,10));
    uint32_t header=BT_H(packet,4),kind=header&3;
    if((flags&32)||!((header>>8)&31)||!kind)ll_sell_under_data(slot);
    else if(kind<=2)ll_get_into_contribution(h,slot);else ll_cntl_show_for_phase(h,slot);
   }
  }
  count=(uint8_t)(count-1);slot=(slot+1)&7;
 }
 return 0;
}
HANDLER(ll_proc_keep_under_diamond)
{
 uint32_t h=dest>>8;BT_H(ble_con_make_on_name[h+1],0)&=~192u;
 if(query(dest)!=20)ll_give_about_aspect[h][71]=31;
 ll_tell_under_slip(dest,0,h,ll_give_about_aspect[h][71]);clear_timer(0x824,dest);clear_timer(0x825,dest);
 empty(1100,1,dest);ll_tell_under_position(h);return 0;
}
HANDLER(ll_proc_pick_within_link)
{uint8_t *m=allocate(0x819,source,dest,8);uint32_t status=12;if(query(dest)){memcpy(m+3,ll_give_about_aspect[le16(p)]+16,5);status=0;}m[0]=status;put16(m+1,le16(p));send(m);return 0;}
HANDLER(ll_proc_do_at_brave)
{
 if(ll_save_under_decision())return 0;
 uint32_t s=query(dest);if(s!=1)return inactive(s)?0:2;
 uint8_t channels[5];ll_read_of_yellow(channels);uint32_t h=dest>>8;uint8_t *link=ll_give_about_aspect[h];
 if(memcmp(link+16,channels,5)){memcpy(link+21,channels,5);ll_go_into_variation(h);}return 0;
}
HANDLER(ll_proc_show_without_audience)
{
 uint32_t s=(uint8_t)(query(dest)-9),h=dest>>8;
 if(s<=3&&((1u<<s)&11)&&!ll_save_under_decision()&&!(ll_give_about_aspect[h][37]&4))ll_cntl_read_within_record(h,p);
 else reject_encrypted(h);
 return 0;
}
#define GUARDED_PAYLOAD(name,target) HANDLER(name) {uint32_t s=query(dest),h=dest>>8; \
 if(!s||s==20)return 0;if(s==21)return 2;if(ll_give_about_aspect[h][37]&16)ll_cntl_pick_without_bid(h,61);else target(h,p);return 0;}
GUARDED_PAYLOAD(ll_proc_write_about_apartment,ll_cntl_find_about_visual)
GUARDED_PAYLOAD(ll_proc_fetch_within_conflict,ll_cntl_cause_within_recommendation)
#define STATE_SINGLE(name,expected,target) HANDLER(name) {uint32_t h=dest>>8; \
 if(query(dest)==expected)target(h);else reject_encrypted(h);return 0;}
STATE_SINGLE(ll_proc_find_under_truth,14,ll_cntl_pick_without_support)
STATE_SINGLE(ll_proc_show_of_present,11,ll_cntl_tell_on_spend)
static uint32_t prepare_encryption(uint32_t h,uint32_t dest,uint32_t s)
{
 if(ll_give_about_aspect[h][37]&16){ll_cntl_pick_without_bid(h,61);return 0;}
 if(ll_save_under_decision()!=1)return 0;
 if(s==3){clear_timer(0x825,dest);ll_give_about_aspect[h][36]=1;}
 uint8_t *link=ll_give_about_aspect[h];link[37]|=24;if(link[70]){state(dest,7);return 2;}return 1;
}
HANDLER(ll_proc_make_at_blame)
{
 uint32_t s=query(dest),h=dest>>8;if(!s||s==20)return 0;if(s==21)return 2;
 if(s!=8){uint32_t result=prepare_encryption(h,(h<<8)|2,s);if(result!=1)return result;}
 ll_cntl_save_under_outcome(h);return 0;
}
#define STATE_PAYLOAD(name,expected,target) HANDLER(name) {uint32_t h=dest>>8; \
 if(query(dest)==expected)target(h,p);else reject_encrypted(h);return 0;}
STATE_PAYLOAD(ll_proc_fetch_within_celebration,2,ll_cntl_sell_into_beat)
STATE_PAYLOAD(ll_proc_pass_in_change,9,ll_cntl_reserve_of_football)
HANDLER(ll_proc_reserve_in_painting)
{
 uint32_t s=query(dest),h=dest>>8;uint8_t *link=ll_give_about_aspect[h];
 if(s==9&&!(link[37]&4)){link[37]=0;ll_leave_without_cry(h,0,26);state((h<<8)|2,1);}else reject_encrypted(h);
 return 0;
}
HANDLER(ll_proc_cause_inside_hate)
{uint32_t s=query(dest),h=dest>>8;if(s==12)return 2;if(s==10)ll_cntl_pick_in_peak(h);else reject_encrypted(h);return 0;}
HANDLER(ll_proc_tell_on_method)
{
 uint32_t s=query(dest),h=dest>>8;if(!s||s==20)return 0;if(s==21)return 2;
 if(s!=8&&s!=16){uint32_t result=prepare_encryption(h,dest,s);if(result!=1)return result;}
 ll_cntl_cause_with_high(h,p);return 0;
}
HANDLER(ll_proc_get_about_friendship)
{uint32_t s=query(dest);if(s&&s!=20)ll_cntl_find_at_crazy(dest>>8,p);return 0;}
#define ROLE_PAYLOAD(name,target) HANDLER(name) {uint32_t s=query(dest),h=dest>>8; \
 if(!s||s==20)return 0;if(s==21)return 2; \
 if(ll_give_about_aspect[h][37]&16)ll_cntl_pick_without_bid(h,61);else if(ll_save_under_decision()==1)target(h,p);return 0;}
ROLE_PAYLOAD(ll_proc_make_inside_impression,ll_cntl_write_by_event)
ROLE_PAYLOAD(ll_proc_pick_on_parent,ll_cntl_take_without_hook)
HANDLER(ll_proc_pass_at_string)
{uint32_t s=query(dest);if(s==1)ll_cntl_write_on_quiet(dest>>8,p[0]);else if(!inactive(s))return 2;return 0;}
HANDLER(ll_proc_reserve_in_promise)
{uint32_t s=query(dest);if(s==1)ll_cntl_find_in_heat(dest>>8,1);else if(!inactive(s))return 2;return 0;}
HANDLER(ll_proc_set_for_honey)
{
 uint32_t s=query(dest),h=dest>>8;if(s==21)s=ll_give_about_aspect[h][14];s=(uint8_t)(s-2);
 if(s==18)return 0;
 if(s==0)ll_send_for_consideration(34,h,ll_give_about_aspect[h]+27);
 else if(s==1)ll_send_under_supermarket(34,h);
 else if(s==7||s==8||s==9||s==10||s==12){if(!ll_save_under_decision())encryption_failure(h,34);}
 ll_cntl_pick_without_bid(h,34);return 0;
}
HANDLER(ll_proc_leave_inside_motor)
{
 uint32_t s=query(dest),h=dest>>8;if(inactive(s)){ll_sell_under_data(p[9]);return 0;}
 if(ll_give_about_aspect[h][37]&16){if(query(dest)==21)return 2;ll_sell_under_data(p[9]);encryption_failure(h,61);ll_cntl_leave_in_objective(h,61);return 0;}
 ((void(*)(void *,uint32_t))(uintptr_t)0x843ac)(p,9);return 1;
}
HANDLER(ll_proc_go_with_parking)
{
 uint32_t s=query(dest),status=12;if(s==21)return 2;
 if(s==13){uint32_t h=le16(p);uint8_t *link=ll_give_about_aspect[h];
  if(link[37]&4)ll_cntl_leave_in_objective(h,6);
  else {if(link[36]){timer(0x825,dest,4000);state(dest,3);}else state(dest,1);ll_cntl_sell_at_community(le16(p),6);}status=0;
 }
 ll_reserve_within_drawing(0x81d,source,dest,status,le16(p));return 0;
}
HANDLER(ll_proc_make_on_sale)
{
 uint32_t s=query(dest),status=12;if(s==21)return 2;
 if(s==13){uint8_t *m=allocate(1052,1,dest,32);memcpy(m,p+2,16);memcpy(m+16,ll_give_about_aspect[le16(p)]+38,16);send(m);state(dest,12);status=0;}
 ll_reserve_within_drawing(0x81c,source,dest,status,le16(p));return 0;
}
HANDLER(ll_proc_sell_by_give)
{
 uint32_t s=query(dest),h=dest>>8;
 if(inactive(s)){ll_proc_go_for_department(0x81b,source,dest,12);return 0;}
 if(s==1){
  if(ll_save_under_decision()||!(ll_give_about_aspect[le16(p)][27]&1)){ll_proc_go_for_department(0x81b,source,dest,12);return 0;}
  uint8_t *link=ll_give_about_aspect[le16(p)];link[37]|=8;if(link[70]){state((h<<8)|2,7);return 2;}
 }else if(s!=8&&s!=15)return 2;
 if(s!=15){ll_proc_go_for_department(0x81b,source,dest,0);uint8_t *link=ll_give_about_aspect[h];
  if(link[37]&3){link[37]|=4;ll_cntl_show_of_interview(h);return 2;}}
 ll_cntl_send_on_meaning(h,p);return 0;
}
HANDLER(ll_proc_cause_for_job)
{uint32_t s=query(dest),status=12;if(s==1){ll_cntl_go_about_stupid(le16(p));status=0;}else if(!inactive(s))return 2;ll_proc_go_for_department(0x81a,source,dest,status);return 0;}
HANDLER(ll_proc_reserve_for_roll)
{
 uint8_t *link=ll_give_about_aspect[le16(p)];uint32_t s=query(dest),status=12;
 if(s==1){
  uint32_t minimum=le16(p+2),maximum=le16(p+4),latency=le16(p+6),timeout=le16(p+8);status=18;
  if(maximum>=minimum&&le16(p+12)>=le16(p+10)&&maximum<=3200&&minimum>=6&&(uint16_t)(timeout-10)<=3190&&latency<=500&&timeout*10>=maximum*5/2*(latency+1)){
   if(ll_save_under_decision())status=12;
   else {uint8_t parameters[12];link[75]=1;ble_con_find_inside_service((void *)(uintptr_t)BT_W(link,0),p,parameters);
    uint8_t *event=(void *)(uintptr_t)BT_W(link,0);BT_H(link,12)=((uint32_t)BT_H(event,48)*BT_H(event,62))/10000+le16(p+8)+2;
    ll_cntl_set_of_reward(le16(p),parameters);status=0;}
  }
 }else if(!inactive(s))return 2;
 ll_make_to_display(0x818,source,dest,status,le16(p));return 0;
}
HANDLER(ll_proc_pick_about_earth)
{
 uint32_t s=query(dest),status=12;if(s==4||s==21)return 2;
 if(!inactive(s)){
  uint32_t reason=p[2];status=18;
  if(reason==5||reason==19||reason==20||reason==21||reason==26||reason==41||reason==59){ll_cntl_leave_in_objective(le16(p),reason);status=0;}
 }
 ll_make_to_display(0x80c,source,dest,status,le16(p));return 0;
}
HANDLER(ll_proc_reserve_without_conflict)
{
 uint8_t *m=allocate(0x80f,source,dest,4);uint32_t s=query(dest);
 if(inactive(s)){m[0]=12;m[3]=0;}
 else {m[0]=0;if(p[2]>1){m[0]=18;m[3]=0;}else m[3]=bb_rf_set_on_inflation(p[2]?0:ble_con_make_on_name[le16(p)+1][12],1);}
 put16(m+1,le16(p));send(m);return 0;
}
HANDLER(ll_proc_pick_for_good)
{
 uint32_t s=query(dest),h=dest>>8;if(s!=12)return s==21?2:0;
 uint8_t *hw=ble_con_make_on_name[h+1];for(uint32_t i=0;i<16;i+=2)BT_H(hw,32+i)=le16(p+1+i);
 if(ll_save_under_decision()==1)ll_cntl_set_to_farm(h);else state(dest,10);return 0;
}
HANDLER(ll_proc_cause_under_value)
{
 uint32_t s=query(dest),h=dest>>8;if(!s||s==20)return 0;if(s==21)return 2;
 uint32_t flags=ll_give_about_aspect[h][37];if(flags&16)ll_cntl_pick_without_bid(h,61);
 else if(!(flags&8))ll_cntl_write_on_quiet(h,p[0]);else {uint8_t *m=allocate(0x829,dest,dest,1);m[0]=p[0];send(m);}
 return 0;
}
HANDLER(ll_proc_give_under_hole)
{
 uint32_t h=dest>>8;
 if(ll_give_about_aspect[h][35]){ll_make_to_display(0x80d,source,dest,0,h);ll_send_under_supermarket(0,h);return 0;}
 uint32_t s=query(dest),status=12;if(s==1){ll_cntl_find_in_heat(le16(p),3);timer(0x825,dest,4000);status=0;}else if(!inactive(s))return 2;
 ll_make_to_display(0x80d,source,(uint16_t)((le16(p)<<8)|2),status,le16(p));return 0;
}
uint8_t ll_show_for_chest __attribute__((section(".btmem.bb.ll"),aligned(4)))=0;
#include "bt_ll_proc_tables.inc"
