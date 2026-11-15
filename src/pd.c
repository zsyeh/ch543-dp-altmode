#include "internal.h"
uint8_t XDATA pd_rx[PD_MAX_FRAME];
uint8_t XDATA pd_tx[PD_MAX_FRAME];
pd_context IDATA pd;
static uint16_t IDATA now;
uint8_t pd_due(uint16_t deadline) { return (uint16_t)(now-deadline)<0x8000u; }
void pd_safe(void) {
 dp_hpd_set(0,0); pd.hpd_pending=0; dp_aux_set(0,pd.orientation);
 dp_mux_set(MUX_SAFE,pd.orientation,pd.video_role); pd.pin=0;
 if(pd.dp==DP_ACTIVE || pd.dp==DP_MUX_WAIT) pd.dp=DP_IDLE;
}
void dp_reset(void) {
 pd_safe(); pd.entered=0; pd.mode_obj=0; pd.pending_cmd=0;
 pd.vdm_wait=0; pd.vdm_retry=0; pd.busy_count=0; pd.dp=DP_IDLE;
 pd.partner_mode=0; pd.partner_status=0; pd.attention=0; pd.exit_request=0; pd.usb_request=0;
}
static void link_reset(void) {
 pd.tx_busy=0; pd.tx_wait=0; pd.tx_id=0; pd.rx_valid=0;
 pd.job=0; pd.after_tx=0; pd.dr_swap=0; pd.rx_soft=0;
}
static void power_restart(void) {
 pd.contract=0;
 pd.pe=pd.power_role?PE_SOURCE_WAIT:PE_WAIT_CAP;
 pd.pe_deadline=(uint16_t)(now+(pd.power_role?150u:500u));
}
void pd_recover(uint8_t error) {
 pd.error=error; dp_reset(); link_reset();
 pd.data_role=pd.power_role;
 pd_phy_hard_reset(); power_restart();
 pd.reset_wait=1; pd.pe_deadline=now+1000u;
 /* Sink waits for a fresh Source_Capabilities; no VBUS switching here. */
}
void pd_queue_control(uint8_t type, uint8_t action) {
 pd.job=type; pd.job_count=0; pd.job_action=action;
}
void pd_queue_data(uint8_t type, uint32_t value) {
 pd.job=type; pd.job_count=1; pd.job_vdo=value; pd.job_action=AFTER_NONE;
}
void pd_queue_vdm(uint16_t svid, uint8_t cmd, uint8_t type,
                  uint8_t obj, uint8_t count, uint32_t vdo) {
 pd.job=PD_VENDOR; pd.job_svid=svid; pd.job_cmd=cmd;
 pd.job_type=type; pd.job_obj=obj; pd.job_count=count;
 pd.job_vdo=vdo; pd.job_action=AFTER_NONE;
}
static void submit_job(void) {
 uint16_t h;
 uint32_t v;
 if(!pd.job || pd.tx_busy) return;
 h=(uint16_t)pd.job | 0x0040u | ((uint16_t)pd.data_role<<5) |
   ((uint16_t)pd.power_role<<8) | ((uint16_t)pd.tx_id<<9) |
   ((uint16_t)pd.job_count<<12);
 wire_put16(pd_tx,h);
 if(pd.job_count) {
  if(pd.job==PD_VENDOR) {
   v=((uint32_t)pd.job_svid<<16)|(uint32_t)0x8000|((uint32_t)pd.job_obj<<8)|
     ((uint32_t)pd.job_type<<6)|pd.job_cmd;
   wire_put32(pd_tx+2,v);
   if(pd.job_count>1) wire_put32(pd_tx+6,pd.job_vdo);
   if(pd.job_count==4) {
    wire_put32(pd_tx+10,0); /* certification test ID: unassigned */
    wire_put32(pd_tx+14,((uint32_t)PRODUCT_PID<<16)|PRODUCT_BCD);
   }
  } else wire_put32(pd_tx+2,pd.job_vdo);
 }
 pd.tx_len=(uint8_t)(2u+4u*pd.job_count); pd.tx_tries=0; pd.tx_wait=0;
 if(pd_phy_send(pd.tx_len)) { pd.tx_busy=1; pd.job=0; pd.after_tx=pd.job_action; pd.link_deadline=now+5u; }
}
static void start_soft_reset(void) {
 /* Soft Reset preserves the power setting and already configured mode. */
 pd.error=ERR_LINK; link_reset();
 pd.pending_cmd=0; pd.vdm_wait=0; pd.vdm_retry=0;
 pd.pe=PE_SOFT_WAIT; pd.pe_deadline=now+40u;
 pd_queue_control(PD_SOFT_RESET,AFTER_SOFT_REQUEST);
}
static void contract_ready(void) {
 pd.pe=PE_READY; pd.contract=1; pd.error=ERR_NONE;
 dp_begin();
}
static void tx_complete(void) {
 uint8_t a=pd.after_tx;
 pd.after_tx=AFTER_NONE;
 if(a==AFTER_SOFT) {
  power_restart();
  if(pd.power_role) pd.pe_deadline=now;
 } else if(a==AFTER_DR) {
  pd.data_role^=1; pd.dr_swap=0; dp_reset(); dp_begin();
 } else if(a==AFTER_ACCEPT) {
  pd.pe=PE_SEND_READY; pd.pe_deadline=now+25u;
 } else if(a==AFTER_READY) contract_ready();
 else if(a==AFTER_SOFT_REQUEST) { pd.pe=PE_SOFT_WAIT; pd.pe_deadline=now+30u; }
 else if(a==AFTER_DR_REQUEST) { pd.dr_swap=2; pd.pe_deadline=now+30u; }
 else dp_tx_complete(a);
}
static void receive_message(void) {
 uint16_t h=wire_get16(pd_rx);
 uint8_t n=(uint8_t)((h>>12)&7u),t=(uint8_t)(h&31u),id=(uint8_t)((h>>9)&7u);
 uint32_t v;
 uint16_t current;
 if((h&0x8000u) || phy_rx_len!=2u+4u*n || ((h>>6)&3u)==0u ||
    ((h>>6)&3u)==3u) return;
 if(!n && t==PD_GOODCRC) return; /* HAL consumes valid GoodCRC */
 if(pd.rx_valid && id==pd.rx_id &&
    (n || t!=PD_SOFT_RESET || pd.rx_soft)) return;
 pd.rx_valid=1; pd.rx_id=id; pd.rx_soft=0;
 /* Every SOP non-GoodCRC packet must carry the opposite power/data roles. */
 if(((h>>8)&1u)==pd.power_role || ((h>>5)&1u)==pd.data_role) {
  pd_recover(ERR_FORMAT); return;
 }
 if(!n && t==PD_SOFT_RESET) {
  pd.pending_cmd=0; pd.vdm_wait=0; pd.vdm_retry=0;
  link_reset(); pd.rx_valid=1; pd.rx_id=id; pd.rx_soft=1;
  pd.contract=0; pd.pe=PE_WAIT_CAP;
  pd_queue_control(PD_ACCEPT,AFTER_SOFT); return;
 }
 if(pd.job) { pd_recover(ERR_FORMAT); return; }
 if(n) {
  if(t==PD_VENDOR) { if(pd.contract) dp_receive(n); return; }
  if(!pd.power_role && t==PD_SOURCE_CAP) {
   v=wire_get32(pd_rx+2);
   /* PDO#1 must be fixed 5V. Request no more than advertised current. */
   if((v&0xc0000000UL) || ((v>>10)&1023u)!=100u || !(v&1023u)) {
    pd_recover(ERR_POWER); return;
   }
   current=(uint16_t)(v&1023u);
   if(current>PD_CURRENT_10MA) current=PD_CURRENT_10MA;
   pd.contract=0; pd.reset_wait=0;
   pd_queue_data(PD_REQUEST,0x13000000UL|((uint32_t)current<<10)|current);
   pd.pe=PE_WAIT_ACCEPT; pd.pe_deadline=now+40u; return;
  }
  if(pd.power_role && t==PD_REQUEST && n==1 && pd.pe==PE_SOURCE_WAIT) {
   v=wire_get32(pd_rx+2);
   if((v>>28)!=1u || (v&0x08f00000UL) || !(v&1023u) ||
      ((v>>10)&1023u)>PD_CURRENT_10MA || (v&1023u)>PD_CURRENT_10MA ||
      ((v>>10)&1023u)>(v&1023u) || !board_vbus_valid())
    pd_queue_control(PD_REJECT,AFTER_NONE);
   else { pd_queue_control(PD_ACCEPT,AFTER_ACCEPT); pd.pe=PE_SEND_ACCEPT; }
  }
  return;
 }
 if(t==PD_ACCEPT) {
  if(pd.pe==PE_SOFT_WAIT) { power_restart(); if(pd.power_role) pd.pe_deadline=now; }
  else if(pd.dr_swap==2) {
   pd.data_role^=1; pd.dr_swap=0; dp_reset(); dp_begin();
  } else if(pd.pe==PE_WAIT_ACCEPT) { pd.pe=PE_WAIT_READY; pd.pe_deadline=now+550u; }
 } else if(t==PD_PS_RDY && pd.pe==PE_WAIT_READY && board_vbus_valid()) {
  contract_ready();
 } else if(t==PD_DR_SWAP) {
  if(pd.entered) { pd_recover(ERR_FORMAT); return; }
  if(pd.contract && !pd.pending_cmd && !pd.tx_busy)
   pd_queue_control(PD_ACCEPT,AFTER_DR);
  else pd_queue_control(PD_REJECT,AFTER_NONE);
 } else if(t==PD_PR_SWAP || t==PD_VCONN_SWAP) {
  pd_queue_control(PD_REJECT,AFTER_NONE);
 } else if(t==PD_GET_SOURCE_CAP && pd.power_role) {
  pd_queue_data(PD_SOURCE_CAP,0x06019000UL|PD_CURRENT_10MA);
 } else if(t==PD_GET_SINK_CAP && !pd.power_role) {
  pd_queue_data(PD_SINK_CAP,0x06019000UL|PD_CURRENT_10MA);
 } else if(t==PD_REJECT || t==PD_WAIT) {
  if(pd.dr_swap) pd.dr_swap=0;
  else if(pd.pe==PE_WAIT_ACCEPT) pd_recover(ERR_POWER);
 }
}
void pd_init(uint8_t video_role, uint8_t power_role, uint8_t prefer_usb3) {
 uint8_t i;
 uint8_t IDATA *p=(uint8_t IDATA *)&pd;
 for(i=0;i<sizeof(pd);i++) p[i]=0;
 pd.video_role=(video_role==DP_SINK)?DP_SINK:DP_SOURCE;
 pd.power_role=power_role?1:0; pd.data_role=pd.power_role;
 pd.prefer_usb3=prefer_usb3?1:0; now=system_get_tick();
 pd_safe(); board_vbus_set(0); pd_phy_disable();
}
uint8_t pd_set_video_role(uint8_t role) {
 if(pd.orientation || (role!=DP_SOURCE && role!=DP_SINK)) return 0;
 pd.video_role=role; pd_safe(); return 1;
}
void pd_request_exit(void) {
 if(pd.entered) {
  pd.exit_request=1;
  if(pd.data_role && pd.pin) pd.usb_request=1;
 }
}
uint8_t pd_request_data_swap(void) {
 if(!pd.contract || pd.entered || pd.pending_cmd || pd.tx_busy || pd.job) return 0;
 pd_queue_control(PD_DR_SWAP,AFTER_DR_REQUEST); pd.dr_swap=1; return 1;
}
static void pd_step(void) {
 uint8_t o,e;
 now=system_get_tick(); o=typec_get_orientation();
 e=pd.orientation?pd_phy_events():0;
 if(pd.orientation && (o==pd.orientation || (!o && !board_vbus_valid())) && (e&PHY_RESET)) {
  dp_reset(); link_reset(); pd.data_role=pd.power_role; power_restart();
  pd.reset_wait=1; pd.pe_deadline=now+1000u; return;
 }
 if(pd.orientation && ((o!=pd.orientation && !(pd.reset_wait && !o && !board_vbus_valid())) ||
     (!board_vbus_valid() && !pd.reset_wait))) {
  dp_reset(); link_reset(); pd.contract=0; pd.pe=PE_OFF; pd.orientation=0;
  pd.data_role=pd.power_role; pd.cc_candidate=0; pd.reset_wait=0;
  pd_phy_disable(); board_vbus_set(0); return;
 }
 if(!pd.orientation) {
  if(o!=pd.cc_candidate) { pd.cc_candidate=o; pd.cc_since=now; }
  if(o && pd_due(pd.cc_since+TYPEC_DEBOUNCE_MS)) {
   if(pd.power_role) {
    if(!board_source_allowed()) { pd.error=ERR_POWER; return; }
    board_vbus_set(1);
   }
   if(!board_vbus_valid()) return;
   pd.orientation=o; pd.data_role=pd.power_role;
   link_reset(); dp_reset(); power_restart(); pd_phy_enable(o);
   if(pd.power_role) pd.pe_deadline=now;
  }
  return;
 }

 if(e&PHY_FAULT) { pd_recover(ERR_LINK); return; }
 if((e&PHY_TX_END) && pd.tx_busy) {
  pd.tx_wait=1; pd.link_deadline=now+PD_ACK_TIMEOUT_MS;
 }
 if((e&PHY_ACK) && pd.tx_busy && phy_ack_id==pd.tx_id) {
  pd.tx_busy=0; pd.tx_wait=0; pd.tx_id=(pd.tx_id+1u)&7u; tx_complete();
 }
 if(e&PHY_RX) { receive_message(); pd_phy_rx_release(); }
 if(pd.tx_busy && !pd.tx_wait && pd_due(pd.link_deadline)) {
  pd_recover(ERR_LINK); return;
 }
 if(pd.tx_busy && pd.tx_wait && pd_due(pd.link_deadline)) {
  if(pd.tx_tries>=PD_TX_RETRIES) {
   if(pd.pe==PE_SOFT_WAIT || pd.after_tx==AFTER_SOFT) pd_recover(ERR_LINK);
   else start_soft_reset();
   return;
  }
  if(pd_phy_send(pd.tx_len)) { pd.tx_tries++; pd.tx_wait=0; pd.link_deadline=now+5u; }
 }
 if(pd.dr_swap==2 && pd_due(pd.pe_deadline)) pd.dr_swap=0;
 if(pd.reset_wait==1u && !board_vbus_valid()) pd.reset_wait=2u;
 if(pd.reset_wait==2u && board_vbus_valid()) {
  pd.reset_wait=0; if(pd.power_role) pd.pe_deadline=now+150u;
 }
 if(pd.reset_wait && pd_due(pd.pe_deadline)) { pd.reset_wait=0; return; }
 if(pd.pe==PE_WAIT_ACCEPT || pd.pe==PE_WAIT_READY || pd.pe==PE_SOFT_WAIT) {
  if(pd_due(pd.pe_deadline)) { pd_recover(ERR_TIMEOUT); return; }
 } else if(pd.pe==PE_SEND_READY && pd_due(pd.pe_deadline) && !pd.tx_busy && !pd.job) {
  if(board_vbus_valid()) pd_queue_control(PD_PS_RDY,AFTER_READY);
  else { pd_recover(ERR_POWER); return; }
 } else if(pd.pe==PE_SOURCE_WAIT && pd_due(pd.pe_deadline) && !pd.tx_busy && !pd.job) {
  pd_queue_data(PD_SOURCE_CAP,0x06019000UL|PD_CURRENT_10MA); pd.pe_deadline=now+150u;
 }
 if(pd.contract && pd.pe==PE_READY && !pd.dr_swap) dp_poll();
 submit_job();
}
void pd_poll(void) {
 pd_step(); /* detach/reset cancels obsolete routing before any I2C commit */
 hal_poll();
}
