/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2025 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "kitty.h"
#include "../engine.h"
#include <string.h>
#include <math.h>

extern "C" {
  #include "sound/kitty.h"
}

#define rWrite(addr,value) {kitty_psg_write(psg,addr,value); if (addr<32) {regPool[addr]=value;}}
#define rWriteVol(c,l,r) {rWrite(0x10+(c&3),((l&15)<<4)+(r&15));}
#define rWriteWav(c,w) {rWrite(0x14+(c&3),w&0xFF);}
#define rWriteFre(c,f) {rWrite(0x03,0x34+((c&3)<<6)); rWrite(c&3,f&0xFF); rWrite(c&3,(f>>8)&0xFF)}
#define CHIP_DIVIDER 8

const char* regCheatSheetKITTY[]={
  "Freqx",             "00+x*3",
  "FreqCtrl",          "03",
  "Volx",              "10+x*4",
  "Wavex",             "14+x*4",
  NULL
};

const char** DivPlatformKITTY::getRegisterSheet() {
  return regCheatSheetKITTY;
}

void DivPlatformKITTY::acquire(short** buf, size_t len) {
  
  for (int i=0; i<4; i++) {
    oscBuf[i]->begin(len);
  }

  for (size_t h=0; h<len; h++) {
    kitty_psg_tick_82c54(psg);
    if (noiseTimer <= 0) {
      kitty_psg_tick_noise(psg);
      noiseTimer = noiseClock;
    } else {
      noiseTimer--;
    }
    short right = kitty_psg_get_sample(psg,0)*128;
    short left = kitty_psg_get_sample(psg,1)*128;
    buf[0][h]=left;
    buf[1][h]=right;
    
    for (int i=0; i<4; i++) {
      oscBuf[i]->putSample(h,
        kitty_psg_get_channel_sample(psg,1,i)*512 +
        kitty_psg_get_channel_sample(psg,0,i)*512
      );
    }
  }
  for (int i=0; i<4; i++) {
    oscBuf[i]->end(len);
  }
  
}

void DivPlatformKITTY::reset() {
  for (int i=0; i<4; i++) {
    chan[i]=Channel();
    chan[i].std.setEngine(parent);
    chan[i].vol=0x0f;
    
  }
  for (int i=0; i<4; i++) {
    rWriteVol(i,0,0);
    rWriteWav(i,0x00);
    if (i < 3)
        rWriteFre(i,0x0000);
  }
}

int DivPlatformKITTY::calcNoteFreq(int note) {
    return parent->calcBaseFreq(chipClock,CHIP_DIVIDER,note,true);
}

void DivPlatformKITTY::rWriteVolPan(int ch,int vol,int pan) {
  int panL = (pan>>4);
  int panR = (pan&15);

  rWriteVol(ch,(vol*panL)/15,(vol*panR)/15);
}


void DivPlatformKITTY::tick(bool sysTick) {
  
  for (int i=0; i<4; i++) {
    chan[i].std.next();
    if (isMuted[i]) {
      rWriteVol(i,0,0);
    }
    if (chan[i].std.vol.had) {
      chan[i].outVol=VOL_SCALE_LINEAR_BROKEN(chan[i].vol&15,MIN(15,chan[i].std.vol.val),15);
      if (chan[i].outVol<0) chan[i].outVol=0;
      if (!isMuted[i]) {
        rWriteVolPan(i,chan[i].outVol,chan[i].pan);
        //rWriteVol(i,chan[i].outVol,chan[i].outVol);
      }
      else {
        rWriteVol(i,0,0);
      }
    }
    if (NEW_ARP_STRAT) {
      chan[i].handleArp();
    } else if (chan[i].std.arp.had) {
      if (!chan[i].inPorta) {
        chan[i].baseFreq=calcNoteFreq(parent->calcArp(chan[i].note,chan[i].std.arp.val));
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].std.wave.had) {
      rWriteWav(i,chan[i].std.wave.val);
    }
    
    if (chan[i].std.panL.had) {
      chan[i].pan&=0x0f;
      chan[i].pan|=(chan[i].std.panL.val&15)<<4;
    }

    if (chan[i].std.panR.had) {
      chan[i].pan&=0xf0;
      chan[i].pan|=chan[i].std.panR.val&15;
    }
    if (chan[i].std.panL.had || chan[i].std.panR.had) {
      if (isMuted[i]) {
        rWrite(i,0);
      } else {
        rWriteVolPan(i,chan[i].outVol,chan[i].pan);
      }
    }

    if (chan[i].std.pitch.had) {
      if (chan[i].std.pitch.mode) {
        chan[i].pitch2+=chan[i].std.pitch.val;
        CLAMP_VAR(chan[i].pitch2,-32768,32767);
      } else {
        chan[i].pitch2=chan[i].std.pitch.val;
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].freqChanged && i < 3) {
      chan[i].freq=parent->calcFreq(
        chan[i].baseFreq,
        chan[i].pitch,
        chan[i].fixedArp?chan[i].baseNoteOverride:chan[i].arpOff,
        chan[i].fixedArp,
        true,
        0,
        chan[i].pitch2,
        chipClock,
        CHIP_DIVIDER
      );
      if (chan[i].freq>65535) chan[i].freq=65535;
      rWriteFre(i,chan[i].freq);
      
      chan[i].freqChanged=false;
    }
  }
  
}

int DivPlatformKITTY::dispatch(DivCommand c) {
  int tmp;
  switch (c.cmd) {
    
    case DIV_CMD_NOTE_ON:
      rWriteVolPan(c.chan,chan[c.chan].vol,chan[c.chan].pan);    
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=calcNoteFreq(c.value);
        chan[c.chan].freqChanged=true;
        chan[c.chan].note=c.value;
      }
      chan[c.chan].active=true;
      chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_VERA));
      if (!parent->song.brokenOutVol && !chan[c.chan].std.vol.will) {
        chan[c.chan].outVol=chan[c.chan].vol;
      }
      break;
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].active=false;
      rWriteVol(c.chan,0,0)
      chan[c.chan].macroInit(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      chan[c.chan].ins=(unsigned char)c.value;
      break;
    case DIV_CMD_VOLUME:
      tmp=c.value&0xf;
      chan[c.chan].vol=tmp;
      if (chan[c.chan].active) {
        rWriteVolPan(c.chan,tmp,chan[c.chan].pan);
      }
      break;
    case DIV_CMD_GET_VOLUME:
      return chan[c.chan].vol;
      break;
    case DIV_CMD_PITCH:
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=calcNoteFreq(c.value2+chan[c.chan].sampleNoteDelta);
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_LEGATO:
      chan[c.chan].baseFreq=NOTE_PERIODIC(c.value+chan[c.chan].sampleNoteDelta+((HACKY_LEGATO_MESS)?(chan[c.chan].std.arp.val):(0)));
      chan[c.chan].freqChanged=true;
      chan[c.chan].note=c.value;
      break;
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_VERA));
      }
      if (!chan[c.chan].inPorta && c.value && chan[c.chan].std.arp.will && !NEW_ARP_STRAT) chan[c.chan].baseFreq=NOTE_PERIODIC(chan[c.chan].note);
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_WAVE:
      rWriteWav(c.chan,c.value);
      break;
    case DIV_CMD_PANNING: {
      chan[c.chan].pan=(c.value&0xf0)|(c.value2>>4);
      rWriteVolPan(c.chan,chan[c.chan].outVol,chan[c.chan].pan);
      break;
    }
    case DIV_CMD_SAMPLE_POS:
      break;
    case DIV_CMD_GET_VOLMAX:
      return 15;
      break;
    case DIV_CMD_MACRO_OFF:
      chan[c.chan].std.mask(c.value,true);
      break;
    case DIV_CMD_MACRO_ON:
      chan[c.chan].std.mask(c.value,false);
      break;
    case DIV_CMD_MACRO_RESTART:
      chan[c.chan].std.restart(c.value);
      break;
    case DIV_CMD_EXTERNAL:
      break;
    default:
      break;
    
  }
  return 1;
}

void* DivPlatformKITTY::getChanState(int ch) {
  return &chan[ch];
}

DivMacroInt* DivPlatformKITTY::getChanMacroInt(int ch) {
  return &chan[ch].std;
}

unsigned short DivPlatformKITTY::getPan(int ch) {
  return ((chan[ch].pan&1)<<8)|((chan[ch].pan&2)>>1);
}

DivDispatchOscBuffer* DivPlatformKITTY::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformKITTY::getRegisterPool() {
  return regPool;
}

int DivPlatformKITTY::getRegisterPoolSize() {
  return 32;
}

bool DivPlatformKITTY::getLegacyAlwaysSetVolume() {
  return false;
}

void DivPlatformKITTY::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  if (isMuted[ch]) {
    rWriteVol(ch,0,0);
  } else {
    if (chan[ch].active) rWriteVolPan(ch,chan[ch].outVol,chan[ch].pan);
  }
}

float DivPlatformKITTY::getPostAmp() {
  return 4.0f;
}

int DivPlatformKITTY::getOutputCount() {
  return 2;
}

void DivPlatformKITTY::notifyInsDeletion(void* ins) {
  for (int i=0; i<4; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformKITTY::poke(unsigned int addr, unsigned short val) {
  rWrite(addr,(unsigned char)val);
}

void DivPlatformKITTY::poke(std::vector<DivRegWrite>& wlist) {
  for (auto &i: wlist) poke(i.addr,i.val);
}

void DivPlatformKITTY::setFlags(const DivConfig& flags) {
  chipClock=3000000;
  CHECK_CUSTOM_CLOCK;

  switch (flags.getInt("clockSel",0)) {
    case 1: // NTSC/PAL
      noiseClock=chipClock/60/8;
      break;
    case 2: // CUSTOM
      noiseClock=chipClock/flags.getInt("noiseClock",50)/8;
      break;
    default: // PAL
      noiseClock=chipClock/50/8;
      break;
  }
  
  rate=chipClock;
  for (int i=0; i<4; i++) {
    oscBuf[i]->setRate(rate);
  }
}

int DivPlatformKITTY::init(DivEngine* p, int channels, int sugRate, const DivConfig& flags) {
  for (int i=0; i<4; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  parent=p;
  psg=new struct kitty_psg;
  dumpWrites=false;
  skipRegisterWrites=false;
  setFlags(flags);
  reset();
  return 4;
}

void DivPlatformKITTY::quit() {
  for (int i=0; i<4; i++) {
    delete oscBuf[i];
  }
  delete psg;
}
DivPlatformKITTY::~DivPlatformKITTY() {
}
