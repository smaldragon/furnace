
#ifndef KITTYSOUND_H
#define KITTYSOUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct kitty_psg{
    uint8_t cycle;
    uint8_t wave[4];
    uint8_t volume[4];
    
    // 82c54
    uint8_t timer_control[3];
    uint16_t timer_reload[3];
    uint16_t timer_count[3];
    uint8_t timer_latch[3];
    uint8_t timer_state[3];
    
    uint8_t timer_output[3];
} kitty_psg;

uint8_t kitty_psg_tick_wave(uint8_t wave);
uint8_t kitty_psg_tick_noise(kitty_psg *psg);
uint8_t kitty_psg_tick_82c54(kitty_psg *psg);
uint8_t kitty_psg_write(kitty_psg *psg, uint16_t address, uint8_t value);
int16_t kitty_psg_get_sample(kitty_psg *psg, int left);
int16_t kitty_psg_get_channel_sample(kitty_psg *psg, int left,int channel);

#ifdef __cplusplus
};
#endif
#endif
