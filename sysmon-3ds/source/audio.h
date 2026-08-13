#pragma once

void audio_init(void);
void audio_exit(void);
void audio_play_click(void);    // short blip — tab switch
void audio_play_confirm(void);  // slightly lower tone — macro/action send
