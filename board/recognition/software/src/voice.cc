#include "voice.h"
#include "voice_messages.h"
#include "bsp.h"

static volatile uint32_t* const voice_regs=reinterpret_cast<volatile uint32_t*>(0xf8100000u);
static bool voice_enabled=false,voice_started=false,voice_sent=false,voice_was_busy=false;
static unsigned voice_event=0;
static uint32_t voice_next_ms=0;
static uint32_t voice_read(unsigned offset) {
  asm volatile("fence iorw,iorw" ::: "memory");return voice_regs[offset/4];
}
static volatile uint32_t* const shared_uart=reinterpret_cast<volatile uint32_t*>(BSP_UART_TERMINAL);
static bool voice_drain(uint32_t start,unsigned char_us) {
  // The FIFO-empty bit excludes the byte in the shifter; leave two character times.
  while(shared_uart[1]&0x8000u) {
    if(static_cast<uint32_t>(voice_read(0x308)-start)>=100000u) {
      voice_enabled=false;return false;
    }
  }
  const uint32_t empty=voice_read(0x308);
  while(static_cast<uint32_t>(voice_read(0x308)-empty)<2u*char_us) {}
  return true;
}
static bool voice_send(const uint8_t* bytes,unsigned size) {
  // shortcut: one bare-metal CPU owns all UART writers; revisit arbitration if tasks/ISRs add writers.
  uint32_t irq;asm volatile("csrrci %0,mstatus,8" : "=r"(irq) :: "memory");
  const uint32_t start=voice_read(0x308);
  bool sent=voice_drain(start,(10000000u+BSP_UART_BAUDRATE-1)/BSP_UART_BAUDRATE);
  if(sent) {
    shared_uart[2]=BSP_CLINT_HZ/(9600u*BSP_UART_DATA_LEN)-1u;
    shared_uart[3]=7u;
    voice_regs[0x464/4]=1;asm volatile("fence iorw,iorw" ::: "memory");
    // All fixed frames fit in the existing 128-byte FIFO.
    for(unsigned i=0;i<size;i++)shared_uart[0]=bytes[i];
    asm volatile("fence iorw,iorw" ::: "memory");
    sent=voice_drain(start,1042u);
    shared_uart[2]=BSP_CLINT_HZ/(BSP_UART_BAUDRATE*BSP_UART_DATA_LEN)-1u;
    shared_uart[3]=7u;
    voice_regs[0x464/4]=0;asm volatile("fence iorw,iorw" ::: "memory");
  }
  if(irq&8u)asm volatile("csrsi mstatus,8" ::: "memory");
  return sent;
}
bool voice_init(uint32_t now_ms) {
  voice_enabled=voice_read(0x468)==0x56544332u;
  voice_started=false;voice_sent=false;voice_was_busy=false;voice_event=0;
  voice_next_ms=now_ms+1000u;
  return voice_enabled;
}
void voice_update(unsigned event_state,uint32_t now_ms) {
  if(!voice_enabled || event_state>2)return;
  if(!voice_started) {
    if(static_cast<int32_t>(now_ms-voice_next_ms)<0)return;
    voice_started=true;
    if(!event_state && !(voice_read(0x460)&2u))voice_send(voice_boot,sizeof(voice_boot));
  }
  const bool playing=(voice_read(0x460)&2u)!=0;
  if(event_state!=voice_event) {
    if(!event_state && voice_sent)voice_send(voice_stop,sizeof(voice_stop));
    voice_event=event_state;voice_sent=false;voice_was_busy=false;voice_next_ms=now_ms;
  }
  if(!event_state || !voice_enabled)return;
  if(voice_sent) {
    if(event_state==1)return;
    if(playing){voice_was_busy=true;return;}
    if(voice_was_busy){voice_was_busy=false;voice_next_ms=now_ms+5000u;return;}
    if(static_cast<int32_t>(now_ms-voice_next_ms)<0)return;
  } else if(event_state==1 && playing)return;
  if(event_state==2)voice_send(voice_red,sizeof(voice_red));
  else voice_send(voice_yellow,sizeof(voice_yellow));
  voice_sent=true;voice_was_busy=false;
  // BUSY may never assert when unplugged; retry only on a later valid red observation.
  voice_next_ms=now_ms+15000u;
}
