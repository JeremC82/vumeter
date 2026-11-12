// Types partages (dans un .h pour que l'IDE Arduino les connaisse avant ses prototypes automatiques)
#pragma once
#include <Arduino.h>

// valeur affichee : attaque rapide, retombee reguliere ; crete tenue puis qui retombe
struct Ballistic {
  float v = 0, peak = 0;
  int hold = 0;
  void update(float target) {
    if (target > v) v += (target - v) * 0.7f; else v = max(target, v - 0.035f);
    if (v >= peak) { peak = v; hold = 25; }
    else if (hold > 0) hold--;
    else peak = max(v, peak - 0.012f);
  }
};

struct Bar {
  int16_t x, w;
  uint8_t ch, band0, band1;  // ch : 0 gauche, 1 droite, 2 les deux
  bool center, dual;         // dual : cretes separees gauche / droite
  float hue;
  Ballistic b, bc[2];        // barre, et cretes par voie (mode double)
  int8_t shown, cap, cap2;
};
