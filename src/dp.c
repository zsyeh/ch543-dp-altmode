#include "internal.h"
uint32_t dp_local_mode(void) {
 uint32_t v=0x44UL; /* receptacle + legacy DP signalling */
 if(pd.video_role==DP_SOURCE) return v|2u|((uint32_t)LOCAL_PINS<<8);
 return v|1u|((uint32_t)LOCAL_PINS<<16);
}
uint16_t dp_local_status(void) {
 uint16_t v=pd.video_role;
 if(!pd.data_role) {
  if(pd.dp==DP_ACTIVE) v|=ST_ENABLED;
  if(pd.prefer_usb3) v|=ST_MULTI;
  if(pd.exit_request) v|=ST_EXIT;
 }
 /* A pre-attached monitor must not trigger AUX access between Configure ACK
  * and the asynchronous MUX readback. Publish its HPD only once lanes/AUX
  * are READY; activated() then queues a fresh Attention/Status notification. */
 if(pd.video_role==DP_SINK && pd.dp==DP_ACTIVE) {
  if(dp_hpd_get()) { v|=ST_HPD; if(pd.irq) v|=ST_IRQ; }
 }
 return v;
}
static void fail(uint8_t error) {
 pd_safe(); pd.error=error; pd.dp=DP_FAILED;
 pd.pending_cmd=0; pd.vdm_wait=0; pd.vdm_retry=0;
 /* Preserve entered until Exit ACK or Hard Reset: a timeout is not an exit. */
}
static uint8_t valid_status(uint32_t v) {
 return !(v&0xfffffe00UL) && !((v&ST_IRQ) && !(v&ST_HPD));
}
static uint8_t complementary(uint16_t s) {
 uint8_t con=(uint8_t)(s&3u);
 return pd.video_role==DP_SOURCE ? (con&DP_SINK) : (con&DP_SOURCE);
}
static void activated(void) {
 pd.dp=DP_ACTIVE;
 if(pd.video_role==DP_SOURCE) {
  dp_hpd_set(0,0); pd.hpd_deadline=system_get_tick()+4u; pd.hpd_pending=1;
 }
 else pd.attention=1;
}
static void apply(void) {
 dp_mux_set(pd.pin,pd.orientation,pd.video_role);
 dp_aux_set(1,pd.orientation);
 pd.dp=DP_MUX_WAIT; pd.hpd_deadline=system_get_tick()+100u;
 pd.hpd_pending=(pd.video_role==DP_SOURCE)?1u:0u;
 if(dp_mux_status()==MUX_READY) activated();
}
static void request(uint8_t cmd) {
 uint32_t v=0;
 uint16_t s=DP_SVID;
 uint8_t obj=pd.mode_obj,n=1;
 if(cmd==VDM_IDENTITY || cmd==VDM_SVIDS) { s=USB_SID; obj=0; }
 else if(cmd==VDM_MODES) obj=0;
 else if(cmd==VDM_STATUS) {
  n=2; v=dp_local_status();
  if(pd.busy_count) v|=pd.status_irq_sent;
  pd.status_irq_sent=(uint16_t)(v&ST_IRQ);
 }
 else if(cmd==VDM_CONFIGURE) {
  n=2;
  /* Configure selects the role of the UFP_U responder. */
  if(pd.pin) v=((uint16_t)pd.pin<<8)|4u|(pd.video_role==DP_SOURCE?2u:1u);
 }
 pd_queue_vdm(s,cmd,VDM_INIT,obj,n,v);
 pd.pending_cmd=cmd; pd.vdm_wait=2; pd.job_action=AFTER_VDM;
}
void dp_begin(void) {
 if(!pd.contract || (pd.entered && (pd.dp==DP_ACTIVE || pd.dp==DP_MUX_WAIT))) return;
 if(pd.data_role) {
  pd.busy_count=0;
  if(pd.entered) { pd.dp=DP_STATUS_WAIT; request(VDM_STATUS); }
  else { pd.dp=DP_DISC_ID; pd.svid_pages=0; request(VDM_IDENTITY); }
 } else pd.dp=DP_IDLE;
}
static uint8_t choose_mode(uint32_t v) {
 uint8_t pins;
 /* Mode capability: bit0=UFP_D, bit1=DFP_D. */
 if(!(v&4u) || (v&0xff000000UL)) return 0;
 if(pd.video_role==DP_SOURCE) {
  if(!(v&1u)) return 0;
  pins=(uint8_t)(v>>((v&0x40u)?16:8));
 } else {
  if(!(v&2u)) return 0;
  pins=(uint8_t)(v>>((v&0x40u)?8:16));
 }
 return pins&LOCAL_PINS;
}
static uint8_t choose_pin(uint8_t pins) {
 if(pd.prefer_usb3 && (pd.partner_status&ST_MULTI) && (pins&PIN_D)) return PIN_D;
 if(pins&PIN_C) return PIN_C;
 if(pins&PIN_E) return PIN_E;
 if(pins&PIN_D) return PIN_D;
 return 0;
}
static void response(uint16_t svid,uint8_t cmd,uint8_t type,uint8_t obj,uint8_t count) {
 uint8_t i,pins,found=0;
 uint32_t v;
 if(!pd.data_role || !pd.pending_cmd || pd.vdm_wait!=1 || cmd!=pd.pending_cmd ||
    svid!=((cmd==VDM_IDENTITY || cmd==VDM_SVIDS)?USB_SID:DP_SVID) ||
    obj!=((cmd<=VDM_MODES)?0:pd.mode_obj)) return;
 if(type==VDM_BUSY) {
  if(count!=1) { fail(ERR_FORMAT); return; }
  if(pd.busy_count>=VDM_BUSY_RETRIES) { fail(ERR_BUSY); return; }
  pd.busy_count++; pd.vdm_wait=0; pd.vdm_retry=1;
  pd.vdm_deadline=system_get_tick()+VDM_BUSY_DELAY_MS; return;
 }
 if(type==VDM_NAK) { if(count==1) fail(ERR_NAK); else fail(ERR_FORMAT); return; }
 if(type!=VDM_ACK) return;
 pd.vdm_wait=0; pd.pending_cmd=0; pd.busy_count=0;
 if(cmd==VDM_IDENTITY) {
  if(count<4 || !(wire_get32(pd_rx+6)&0x04000000UL)) { fail(ERR_FORMAT); return; }
  pd.dp=DP_DISC_SVID; request(VDM_SVIDS);
 } else if(cmd==VDM_SVIDS) {
  if(count<2) { fail(ERR_FORMAT); return; }
  /* Each SVID pair is high16 first, then low16. Zero terminates the list. */
  for(i=1;i<count;i++) {
   v=wire_get32(pd_rx+2u+4u*i);
   if((uint16_t)(v>>16)==DP_SVID) found=1;
   if(!(uint16_t)(v>>16)) break;
   if((uint16_t)v==DP_SVID) found=1;
   if(!(uint16_t)v) break;
  }
  if(found) { pd.dp=DP_DISC_MODE; request(VDM_MODES); }
  else if(i==count && count==7 && ++pd.svid_pages<4) request(VDM_SVIDS);
  else fail(ERR_NO_SVID);
 } else if(cmd==VDM_MODES) {
  if(count<2) { fail(ERR_FORMAT); return; }
  for(i=1;i<count;i++) {
   v=wire_get32(pd_rx+2u+4u*i); pins=choose_mode(v);
   if(pins) { pd.partner_mode=v; pd.mode_obj=i; break; }
  }
  if(i==count) { fail(ERR_PINS); return; }
  pd.dp=DP_ENTERING; request(VDM_ENTER);
 } else if(cmd==VDM_ENTER) {
  if(count!=1) { fail(ERR_FORMAT); return; }
  pd.entered=1; pd.dp=DP_STATUS_WAIT; request(VDM_STATUS);
 } else if(cmd==VDM_STATUS) {
  if(count!=2) { fail(ERR_FORMAT); return; }
  v=wire_get32(pd_rx+6);
  if(!valid_status(v) || !complementary((uint16_t)v)) { fail(ERR_FORMAT); return; }
  if((v&ST_HPD) && pd.hpd_pending) v|=pd.partner_status&ST_IRQ;
  pd.partner_status=(uint16_t)v;
  if(v&ST_EXIT) {
   pd.exit_request=1; if(pd.pin) pd.usb_request=1; return;
  }
  if(v&ST_USB) { pd.usb_request=1; return; }
  if(pd.dp==DP_ACTIVE) {
   if(pd.video_role==DP_SOURCE && !pd.hpd_pending) {
    dp_hpd_set((v&ST_HPD)?1:0,(v&ST_IRQ)?1:0);
    pd.partner_status&=(uint16_t)~ST_IRQ;
   }
   return;
  }
  pd.pin=choose_pin(choose_mode(pd.partner_mode));
  if(!pd.pin) { fail(ERR_PINS); return; }
  pd.dp=DP_CONFIG_WAIT; request(VDM_CONFIGURE);
 } else if(cmd==VDM_CONFIGURE) {
  if(count!=1) { fail(ERR_FORMAT); return; }
  if(pd.pin) apply(); else { pd_safe(); pd.dp=DP_IDLE; }
 } else if(cmd==VDM_EXIT) {
  if(count!=1) { fail(ERR_FORMAT); return; }
  dp_reset(); /* Exit stops automatic re-entry until another contract. */
 }
}
static void reply(uint16_t svid,uint8_t cmd,uint8_t obj,uint8_t type,uint8_t count,uint32_t v) {
 pd_queue_vdm(svid,cmd,type,obj,count,v);
}
static void incoming(uint16_t svid,uint8_t cmd,uint8_t obj,uint8_t count) {
 uint32_t v=0;
 uint8_t pin;
 /* Attention is a notification, never receives a VDM ACK/NAK. */
 if(cmd==VDM_ATTENTION) {
  if(!pd.data_role || svid!=DP_SVID || !pd.entered || obj!=pd.mode_obj || count!=2) return;
  v=wire_get32(pd_rx+6);
  if(!valid_status(v)) { fail(ERR_FORMAT); return; }
  if((v&ST_HPD) && pd.hpd_pending) v|=pd.partner_status&ST_IRQ;
  pd.partner_status=(uint16_t)v;
  if(v&ST_EXIT) {
   if(pd.data_role) { pd.exit_request=1; if(pd.pin) pd.usb_request=1; }
   pd_safe(); return;
  }
  if(v&ST_USB) {
   pd_safe(); if(pd.data_role) pd.usb_request=1; return;
  }
  if(!complementary((uint16_t)v)) { pd_safe(); return; }
  if(pd.dp==DP_ACTIVE && pd.video_role==DP_SOURCE && !pd.hpd_pending) {
   dp_hpd_set((v&ST_HPD)?1:0,(v&ST_IRQ)?1:0);
   pd.partner_status&=(uint16_t)~ST_IRQ;
  }
  return;
 }
 /* PD2 Structured VDM discovery/configuration initiator is USB DFP. */
 if(pd.data_role) return;
 if(cmd==VDM_IDENTITY && svid==USB_SID && obj==0 && count==1) {
  reply(svid,cmd,obj,VDM_ACK,4,0x14000000UL|PRODUCT_VID);
 } else if(cmd==VDM_SVIDS && svid==USB_SID && obj==0 && count==1) {
  reply(svid,cmd,obj,VDM_ACK,2,0xff010000UL);
 } else if(cmd==VDM_MODES && svid==DP_SVID && obj==0 && count==1) {
  reply(svid,cmd,obj,VDM_ACK,2,dp_local_mode());
 } else if(cmd==VDM_ENTER && svid==DP_SVID && obj==1 && count==1 && !pd.entered) {
  pd.entered=1; pd.mode_obj=1; pd_safe();
  reply(svid,cmd,obj,VDM_ACK,1,0);
 } else if(cmd==VDM_EXIT && svid==DP_SVID && (obj==1 || obj==7) && count==1 && pd.entered) {
  pd_safe(); reply(svid,cmd,obj,VDM_ACK,1,0); pd.job_action=AFTER_EXIT;
 } else if(cmd==VDM_STATUS && svid==DP_SVID && obj==pd.mode_obj && pd.entered && count==2) {
  v=wire_get32(pd_rx+6);
  if(!valid_status(v)) { fail(ERR_FORMAT); reply(svid,cmd,obj,VDM_NAK,1,0); return; }
  if((v&ST_HPD) && pd.hpd_pending) v|=pd.partner_status&ST_IRQ;
  pd.partner_status=(uint16_t)v;
  if(!complementary((uint16_t)v)) pd_safe();
  if(pd.dp==DP_ACTIVE && pd.video_role==DP_SOURCE && !pd.hpd_pending) {
   dp_hpd_set((v&ST_HPD)?1:0,(v&ST_IRQ)?1:0);
   pd.partner_status&=(uint16_t)~ST_IRQ;
  }
  reply(svid,cmd,obj,VDM_ACK,2,dp_local_status());
 } else if(cmd==VDM_CONFIGURE && svid==DP_SVID && obj==pd.mode_obj && pd.entered && count==2) {
  v=wire_get32(pd_rx+6); pin=(uint8_t)(v>>8);
  pd_safe(); /* disable during every reconfiguration */
  if(v==0) { reply(svid,cmd,obj,VDM_ACK,1,0); pd.dp=DP_IDLE; return; }
  if((v&0xffff00c0UL) || (v&0x3cu)!=4u ||
     (v&3u)!=pd.video_role || !pin || (pin&(pin-1u)) ||
     !(pin&LOCAL_PINS) || !complementary(pd.partner_status)) {
   reply(svid,cmd,obj,VDM_NAK,1,0); pd.error=ERR_PINS; return;
  }
  pd.pin=pin;
  reply(svid,cmd,obj,VDM_ACK,1,0); pd.job_action=AFTER_CONFIG;
 } else reply(svid,cmd,obj,VDM_NAK,1,0);
}
void dp_receive(uint8_t count) {
 uint32_t h=wire_get32(pd_rx+2);
 uint16_t svid=(uint16_t)(h>>16);
 uint8_t cmd=(uint8_t)(h&31u),type=(uint8_t)((h>>6)&3u),obj=(uint8_t)((h>>8)&7u);
 if(!(h&0x8000u) || (h&0x7820u)) return; /* SVDM1.0, reserved bits zero */
 if(type==VDM_INIT) incoming(svid,cmd,obj,count);
 else response(svid,cmd,type,obj,count);
}
void dp_tx_complete(uint8_t action) {
 if(action==AFTER_VDM) {
  pd.vdm_wait=1;
  pd.vdm_deadline=system_get_tick()+
   ((pd.pending_cmd==VDM_ENTER || pd.pending_cmd==VDM_EXIT) ?
    VDM_MODE_TIMEOUT_MS : VDM_TIMEOUT_MS);
 } else if(action==AFTER_CONFIG) apply();
 else if(action==AFTER_EXIT) dp_reset();
}
void dp_poll(void) {
 uint8_t h;
 if(pd.dp==DP_MUX_WAIT) {
  h=dp_mux_status();
  if(h==MUX_FAULT || pd_due(pd.hpd_deadline)) { fail(ERR_HARDWARE); return; }
  if(h==MUX_READY) activated();
 }
 if(pd.hpd_pending && pd.dp==DP_ACTIVE && pd_due(pd.hpd_deadline)) {
  pd.hpd_pending=0;
  dp_hpd_set((pd.partner_status&ST_HPD)?1:0,(pd.partner_status&ST_IRQ)?1:0);
  pd.partner_status&=(uint16_t)~ST_IRQ;
 }
 if(pd.video_role==DP_SINK && pd.entered) {
  h=pd.dp==DP_ACTIVE?dp_hpd_get():0;
  if(h!=pd.hpd) { pd.hpd=h; pd.attention=1; }
  if(dp_hpd_irq_get() && h) { pd.irq=1; pd.attention=1; }
  if(!h) pd.irq=0; /* discard pre-route/low-level IRQs, never send IRQ with HPD=0 */
 }
 if(pd.vdm_wait==1 && pd_due(pd.vdm_deadline)) { fail(ERR_TIMEOUT); return; }
 if(pd.tx_busy || pd.job) return;
 if(pd.vdm_retry) {
  if(pd_due(pd.vdm_deadline)) { pd.vdm_retry=0; request(pd.pending_cmd); }
  return;
 }
 if(pd.pending_cmd) return;
 if(pd.usb_request && pd.entered && pd.data_role) {
  pd_safe(); pd.usb_request=0; pd.dp=DP_CONFIG_WAIT; request(VDM_CONFIGURE); return;
 }
 if(pd.exit_request && pd.entered && pd.data_role) {
  pd_safe(); pd.dp=DP_EXITING; request(VDM_EXIT); return;
 }
 if(pd.entered && (pd.attention || pd.exit_request)) {
  if(pd.data_role) {
   if(pd.video_role!=DP_SINK) return;
   request(VDM_STATUS);
  } else pd_queue_vdm(DP_SVID,VDM_ATTENTION,VDM_INIT,pd.mode_obj,2,dp_local_status());
  pd.attention=0; pd.irq=0; pd.exit_request=0;
 }
}
