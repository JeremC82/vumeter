/**************************************************************************************************/
/*  VU-metre / analyseur de spectre sur dalles LED HUB75 - ESP32 (sans wifi)                     */
/*                                                                                                */
/*  Dalles (choix au bouton, garde en memoire) :                                                 */
/*    0 : 1 dalle 64x64              -> ecran 64 x 64                                             */
/*    1 : 2 dalles 64x64 chainees    -> ecran 128 x 64                                            */
/*    2 : 1 dalle 128x64             -> ecran 128 x 64                                            */
/*    3 : 2 dalles 128x64 chainees   -> ecran 256 x 64                                            */
/*                                                                                                */
/*  Son : entree ligne stereo analogique sur GPIO 36 (gauche) et GPIO 39 (droite), centree a     */
/*  ~1,3 V par un condensateur et deux resistances (voir docs/branchement.png). Lue en continu    */
/*  par l'ADC1 via l'I2S0 (DMA), sans charger le processeur. La dalle utilise l'I2S1.            */
/*                                                                                                */
/*  Bouton sur GPIO 33 (vers la masse) ou bouton BOOT de la carte :                              */
/*    appui court        : mode suivant (Auto, Spectre, Miroir, Aiguilles, ...)                   */
/*    appui 1 a 5 s      : luminosite suivante (5 niveaux)                                       */
/*    appui 5 s          : configuration de dalle suivante (redemarre) - secours du bouton 2      */
/*                                                                                                */
/*  Bouton 2 "dalle" sur GPIO 34 (meme broche que le bouton reset wifi de l'horloge) : vers le    */
/*  3,3 V avec une resistance de 10 k vers la masse (GPIO 34 n'a pas de rappel interne).          */
/*  Chaque appui passe a la dalle suivante                                                       */
/*  (64x64 -> 2x64x64 -> 128x64 -> 2x128x64), le choix est garde et applique 2 s apres le dernier */
/*  appui (redemarrage).                                                                         */
/*                                                                                                */
/*  Mode demo : cavalier permanent entre GPIO 18 et la masse. Le son est alors une petite        */
/*  musique synthetisee (batterie, basse, arpege stereo) a la place de l'entree audio.           */
/*  Le cavalier peut etre mis ou retire a tout moment.                                           */
/*                                                                                                */
/*  Moniteur serie (115200 bauds) : petit menu (taper ?) pour choisir la dalle (1-4), lancer ou   */
/*  arreter la demo (d, memorise), changer de mode (m) ou de luminosite (l).                     */
/**************************************************************************************************/
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <driver/i2s.h>
#include <driver/adc.h>
#include <soc/syscon_struct.h>
#include "vumeter_types.h"

// ---------------------------------------------------------------- brochage (identique a l'horloge)
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 19
#define C_PIN 5
#define D_PIN 17
#define E_PIN 32  // obligatoire pour les dalles 1/32 (64x64 et 128x64)
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16
#define BTN_PIN 33   // bouton vers la masse (pull-up interne)
#define BOOT_PIN 0   // bouton BOOT de la carte, meme role
#define DEMO_PIN 18  // cavalier vers la masse = mode demo (pull-up interne)
#define LAYOUT_BTN_PIN 34  // bouton 2 vers le 3,3 V + 10 k vers la masse (comme le reset wifi de l'horloge)
#define VU_VERSION "1.39"
// entrees audio : ADC1 canal 0 = GPIO 36 (gauche), canal 3 = GPIO 39 (droite)
#define ADC_CH_L ADC1_CHANNEL_0
#define ADC_CH_R ADC1_CHANNEL_3

// ---------------------------------------------------------------- reglages
#define N_LAYOUTS 5
const char *LAYOUT_NAME[N_LAYOUTS] = { "64x64", "2x64x64", "128x64", "2x128x64", "128x128" };
const uint8_t LAYOUT_PANEL_W[N_LAYOUTS] = { 64, 64, 128, 128, 128 };
const uint8_t LAYOUT_CHAIN[N_LAYOUTS] = { 1, 2, 1, 2, 2 };
const bool LAYOUT_STACKED[N_LAYOUTS] = { false, false, false, false, true };  // 128x128 : 2 dalles l'une sous l'autre
// (nouveaux modes ajoutes a la fin : les numeros deja enregistres restent valables)
enum Mode : uint8_t { M_AUTO, M_SPECTRUM, M_MIRROR, M_NEEDLES, M_STEREO, M_SPECTRUM_ST, M_GAME, M_POWER, M_WATTS, M_COUNT };
const char *MODE_NAME[M_COUNT] = { "Auto", "Spectre", "Miroir", "Aiguilles", "Stereo", "Spectre St", "Jeu", "Puissance", "Watts" };
const uint8_t BRIGHT[5] = { 20, 45, 80, 140, 220 };
#define AUTO_SECONDS 20

Preferences prefs;
MatrixPanel_I2S_DMA *dma = nullptr;
// 2 dalles 128x64 chainees et posees l'une sous l'autre (meme sens, la 1re reliee a l'ESP32 en haut) :
// la chaine fait 256x64, on la presente comme un ecran 128x128.
class StackedPanels : public Adafruit_GFX {
 public:
  StackedPanels(MatrixPanel_I2S_DMA *d) : Adafruit_GFX(128, 128), m(d) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || x >= 128 || y < 0 || y >= 128) return;
    if (y < 64) m->drawPixel(x, y, c);
    else m->drawPixel(x + 128, y - 64, c);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    int x0 = max(0, (int)x), x1 = min(128, x + w), y0 = max(0, (int)y), y1 = min(128, y + h);
    if (x1 <= x0 || y1 <= y0) return;
    if (y0 < 64) m->fillRect(x0, y0, x1 - x0, min(y1, 64) - y0, c);
    if (y1 > 64) m->fillRect(x0 + 128, max(y0, 64) - 64, x1 - x0, y1 - max(y0, 64), c);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
  void fillScreen(uint16_t c) override { m->fillScreen(c); }
 private:
  MatrixPanel_I2S_DMA *m;
};
Adafruit_GFX *gfx = nullptr;  // ecran utilise pour dessiner (la dalle, ou les 2 dalles empilees)
int W = 64;               // largeur de l'ecran (64, 128 ou 256)
int H = 64;  // 128 avec 2 dalles empilees
uint8_t layout = 0, mode = M_AUTO, brightIdx = 2;

// ---------------------------------------------------------------- audio partage (tache audio -> affichage)
#define FFT_N 512
#define NBANDS 64
#define WAVE_N 512
struct AudioFrame {
  float rmsDb[2];             // niveau efficace en dB relatifs a la pleine echelle
  float peakDb[2];            // crete du bloc
  float bandDb[2][NBANDS];    // spectre par canal, 64 bandes logarithmiques
  int16_t wave[2][WAVE_N];    // forme d'onde (centree)
  float fs;                   // frequence d'echantillonnage mesuree par canal
  uint32_t seq;
};
AudioFrame shared, view;
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool audioMono = false;  // l'ADC ne donne qu'une voie : copiee sur les deux

// ---------------------------------------------------------------- petite FFT reelle (radix 2)
float fftRe[FFT_N], fftIm[FFT_N], hann[FFT_N], twCos[FFT_N / 2], twSin[FFT_N / 2];
void fftInit() {
  for (int i = 0; i < FFT_N; i++) hann[i] = 0.5f - 0.5f * cosf(2 * PI * i / (FFT_N - 1));
  for (int i = 0; i < FFT_N / 2; i++) { twCos[i] = cosf(2 * PI * i / FFT_N); twSin[i] = -sinf(2 * PI * i / FFT_N); }
}
void fft(float *re, float *im) {
  for (int i = 1, j = 0; i < FFT_N; i++) {  // permutation bit-reverse
    int bit = FFT_N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= FFT_N; len <<= 1) {
    int step = FFT_N / len;
    for (int i = 0; i < FFT_N; i += len)
      for (int k = 0; k < len / 2; k++) {
        float wr = twCos[k * step], wi = twSin[k * step];
        int a = i + k, b = a + len / 2;
        float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr; im[a] += xi;
      }
  }
}

// ---------------------------------------------------------------- acquisition (I2S0 + ADC1, 2 voies)
void adcInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  cfg.sample_rate = 44100;  // ~22 kHz par voie (la frequence reelle est mesuree)
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 512;
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC_CH_L, ADC_ATTEN_DB_12);
  adc1_config_channel_atten(ADC_CH_R, ADC_ATTEN_DB_12);
  i2s_set_adc_mode(ADC_UNIT_1, ADC_CH_L);
  i2s_adc_enable(I2S_NUM_0);
  // table de balayage a 2 entrees : gauche puis droite (canal << 4 | 12 bits << 2 | 11 dB)
  uint32_t eL = (ADC_CH_L << 4) | (3 << 2) | 3, eR = (ADC_CH_R << 4) | (3 << 2) | 3;
  SYSCON.saradc_ctrl.sar1_patt_len = 1;
  SYSCON.saradc_sar1_patt_tab[0] = (eL << 24) | (eR << 16) | (eL << 8) | eR;
}

// 64 bandes logarithmiques de 40 Hz a 16 kHz (limitees par la frequence d'echantillonnage)
uint16_t bandLo[NBANDS], bandHi[NBANDS];
void bandsInit(float fs) {
  float df = fs / FFT_N, fmin = max(40.0f, 1.5f * df), fmax = min(16000.0f, fs * 0.47f);
  for (int b = 0; b < NBANDS; b++) {
    float f0 = fmin * powf(fmax / fmin, (float)b / NBANDS), f1 = fmin * powf(fmax / fmin, (float)(b + 1) / NBANDS);
    bandLo[b] = max(1, (int)(f0 / df));
    bandHi[b] = max((int)bandLo[b] + 1, (int)(f1 / df));
    if (bandHi[b] > FFT_N / 2) bandHi[b] = FFT_N / 2;
  }
}

// ---------------------------------------------------------------- mode demo : musique synthetisee
// Remplace les echantillons lus par l'ADC (meme format : canal << 12 | valeur 12 bits), ce qui
// garde le rythme et la mesure de frequence de l'I2S : tout le reste de la chaine est inchange.
volatile bool demoOn = false;
const float DEMO_BPM = 124;
struct DemoSynth {
  float stepPos = 0;  // avancement dans la double-croche en cours (0..1)
  uint32_t step = 0;  // numero de double-croche
  bool started = false;
  float phK = 0, fK = 45, envK = 0;     // grosse caisse
  float phS = 0, envS = 0;              // caisse claire
  float envH = 0, nPrev = 0;            // charleston
  float phB = 0, fB = 55, envB = 0;     // basse
  float phA = 0, fA = 440, envA = 0, panA = 0.5f;  // arpege
  float level = 1;
  uint32_t rnd = 12345;
} demo;
float midiHz(int n) { return 440.0f * powf(2, (n - 69) / 12.0f); }
float demoNoise() {
  demo.rnd = demo.rnd * 1664525u + 1013904223u;
  return (int32_t)demo.rnd / 2147483648.0f;
}
void demoStep() {  // evenements de la double-croche
  static const uint8_t BASS[4] = { 33, 29, 31, 28 };  // La, Fa, Sol, Mi
  static const uint8_t CHORD[4][4] = { { 69, 72, 76, 81 }, { 65, 69, 72, 77 }, { 67, 71, 74, 79 }, { 64, 67, 71, 76 } };
  uint32_t bar = demo.step / 16, s = demo.step % 16, sect = (bar / 8) % 4, chord = bar % 4;
  bool full = sect != 1;  // section 1 : pont sans batterie ni basse
  if (full && s % 4 == 0) { demo.envK = 1; demo.fK = 170; }
  if (full && (s == 4 || s == 12)) demo.envS = 1;
  if (s % 4 == 2) demo.envH = 1;
  else if (sect >= 2 && (s & 1)) demo.envH = 0.45f;
  if (full && s % 2 == 0) { demo.envB = 1; demo.fB = midiHz(BASS[chord] + (s % 8 == 6 ? 12 : 0)); }
  demo.envA = 1;
  demo.fA = midiHz(CHORD[chord][(s * 3) % 4] + (bar % 2 ? 12 : 0));
  demo.panA = (s & 1) ? 0.2f : 0.8f;
  demo.level = sect == 1 ? 0.35f + 0.6f * (bar % 8) / 8.0f : sect == 3 ? 1.0f : 0.75f;
}
void demoFill(uint16_t *raw, int n, float fs) {
  const float dt = 1.0f / fs, TWO_PI_F = 6.2831853f;
  const float dK = expf(-dt * 7), dKf = expf(-dt * 35), dS = expf(-dt * 16), dH = expf(-dt * 55), dB = expf(-dt * 6), dA = expf(-dt * 11);
  const float stepInc = dt * DEMO_BPM / 15;  // 4 double-croches par temps
  for (int i = 0; i + 1 < n; i += 2) {
    if (!demo.started) { demo.started = true; demoStep(); }
    demo.stepPos += stepInc;
    if (demo.stepPos >= 1) { demo.stepPos -= 1; demo.step++; demoStep(); }
    // grosse caisse : sinus qui descend de 170 a 45 Hz
    demo.fK = 45 + (demo.fK - 45) * dKf;
    demo.phK += TWO_PI_F * demo.fK * dt;
    if (demo.phK > TWO_PI_F) demo.phK -= TWO_PI_F;
    float kick = sinf(demo.phK) * demo.envK;
    // caisse claire : bruit + 190 Hz
    demo.phS += TWO_PI_F * 190 * dt;
    if (demo.phS > TWO_PI_F) demo.phS -= TWO_PI_F;
    float nz = demoNoise();
    float snare = (nz * 0.55f + sinf(demo.phS) * 0.3f) * demo.envS;
    // charleston : bruit aigu (difference de deux tirages)
    float hat = (nz - demo.nPrev) * 0.22f * demo.envH;
    demo.nPrev = nz;
    // basse : 3 harmoniques
    demo.phB += TWO_PI_F * demo.fB * dt;
    if (demo.phB > TWO_PI_F) demo.phB -= TWO_PI_F;
    float bass = (sinf(demo.phB) + 0.5f * sinf(2 * demo.phB) + 0.3f * sinf(3 * demo.phB)) * 0.3f * demo.envB;
    // arpege : sinus + octave, alterne gauche / droite
    demo.phA += TWO_PI_F * demo.fA * dt;
    if (demo.phA > TWO_PI_F) demo.phA -= TWO_PI_F;
    float arp = (sinf(demo.phA) + 0.3f * sinf(2 * demo.phA)) * 0.3f * demo.envA;
    float center = kick * 0.8f + snare + bass;
    float l = (center + hat * 0.7f + arp * (1 - demo.panA)) * demo.level;
    float r = (center + hat + arp * demo.panA) * demo.level;
    demo.envK *= dK; demo.envS *= dS; demo.envH *= dH; demo.envB *= dB; demo.envA *= dA;
    int vl = constrain((int)(2048 + l * 1100), 0, 4095), vr = constrain((int)(2048 + r * 1100), 0, 4095);
    raw[i] = (ADC_CH_L << 12) | vl;
    raw[i + 1] = (ADC_CH_R << 12) | vr;
  }
}

void audioTask(void *) {
  static uint16_t raw[1024];
  static float ringL[FFT_N], ringR[FFT_N];
  static float bands[2][NBANDS];
  int pos = 0, fresh = 0;
  float dc[2] = { 2048, 2048 }, sumSq[2] = { 0, 0 }, pk[2] = { 0, 0 };
  int nSum = 0;
  uint32_t count = 0, t0 = millis();
  float fs = 22050;
  bandsInit(fs);
  for (;;) {
    size_t got = 0;
    i2s_read(I2S_NUM_0, raw, sizeof(raw), &got, portMAX_DELAY);
    int n = got / 2, seenR = 0;
    if (demoOn) demoFill(raw, n, fs);
    float lastL = 0;
    for (int i = 0; i < n; i++) {
      uint16_t w = raw[i];
      int ch = w >> 12, v = w & 0x0FFF;
      int c;
      if (ch == ADC_CH_L) c = 0;
      else if (ch == ADC_CH_R) { c = 1; seenR++; }
      else continue;
      dc[c] += (v - dc[c]) * 0.0005f;  // centre du signal suivi lentement
      float s = v - dc[c];
      sumSq[c] += s * s;
      if (fabsf(s) > pk[c]) pk[c] = fabsf(s);
      if (c == 0) {
        lastL = s;
        if (audioMono) {  // une seule voie : copiee a droite
          sumSq[1] += s * s;
          if (fabsf(s) > pk[1]) pk[1] = fabsf(s);
          ringR[pos] = s;
        }
        ringL[pos] = s;
        count++;
        nSum++;
      } else {
        ringR[pos] = s;
        pos = (pos + 1) % FFT_N;
        fresh++;
      }
      if (c == 0 && audioMono) { pos = (pos + 1) % FFT_N; fresh++; }
    }
    (void)lastL;
    if (n > 200) audioMono = seenR == 0;
    // frequence d'echantillonnage reelle, mesuree chaque seconde
    uint32_t now = millis();
    if (now - t0 >= 1000) {
      float f = count * 1000.0f / (now - t0);
      if (fabsf(f - fs) > fs * 0.03f && f > 4000) { fs = f; bandsInit(fs); }
      count = 0;
      t0 = now;
    }
    if (fresh < FFT_N / 2) continue;  // une analyse toutes les 256 nouvelles valeurs (recouvrement 50 %)
    fresh = 0;
    for (int c = 0; c < 2; c++) {
      float *ring = c ? ringR : ringL;
      for (int i = 0; i < FFT_N; i++) {
        fftRe[i] = ring[(pos + i) % FFT_N] * hann[i];
        fftIm[i] = 0;
      }
      fft(fftRe, fftIm);
      const float norm = 1.0f / (FFT_N / 4 * 2048.0f);  // sinus pleine echelle -> 1
      for (int b = 0; b < NBANDS; b++) {
        float m = 0;
        for (int k = bandLo[b]; k < bandHi[b]; k++) {
          float a = fftRe[k] * fftRe[k] + fftIm[k] * fftIm[k];
          if (a > m) m = a;
        }
        bands[c][b] = 10 * log10f(m * norm * norm + 1e-12f);
      }
    }
    float rms[2], peak[2];
    for (int c = 0; c < 2; c++) {
      rms[c] = 20 * log10f(sqrtf(sumSq[c] / max(1, nSum)) / 2048.0f + 1e-6f);
      peak[c] = 20 * log10f(pk[c] / 2048.0f + 1e-6f);
      sumSq[c] = 0;
      pk[c] = 0;
    }
    nSum = 0;
    portENTER_CRITICAL(&audioMux);
    memcpy(shared.rmsDb, rms, sizeof(rms));
    memcpy(shared.peakDb, peak, sizeof(peak));
    memcpy(shared.bandDb, bands, sizeof(bands));
    for (int i = 0; i < WAVE_N; i++) {
      shared.wave[0][i] = (int16_t)ringL[(pos + i) % FFT_N];
      shared.wave[1][i] = (int16_t)ringR[(pos + i) % FFT_N];
    }
    shared.fs = fs;
    shared.seq++;
    portEXIT_CRITICAL(&audioMux);
  }
}

// ---------------------------------------------------------------- couleurs
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return dma->color565(r, g, b); }
uint16_t hsv(float h, float s, float v) {  // h 0..360
  float c = v * s, x = c * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = v - c, r, g, b;
  if (h < 60) { r = c; g = x; b = 0; } else if (h < 120) { r = x; g = c; b = 0; } else if (h < 180) { r = 0; g = c; b = x; }
  else if (h < 240) { r = 0; g = x; b = c; } else if (h < 300) { r = x; g = 0; b = c; } else { r = c; g = 0; b = x; }
  return rgb((r + m) * 255, (g + m) * 255, (b + m) * 255);
}
// ---------------------------------------------------------------- gain automatique et dynamique des barres
float agcRef = -20;   // niveau (dB) affiche en haut de l'echelle, suit la musique
const float RANGE_DB = 45;
const float GATE_DB = -62;  // en dessous : silence
void agcUpdate(float loudest) {
  if (loudest < GATE_DB) return;
  if (loudest > agcRef) agcRef += (loudest - agcRef) * 0.25f;  // monte vite
  else agcRef -= 0.03f;                                          // redescend doucement
  agcRef = constrain(agcRef, -45.0f, 0.0f);
}
float norm01(float db) { return constrain((db - (agcRef - RANGE_DB)) / RANGE_DB, 0.0f, 1.0f); }

// ---------------------------------------------------------------- moteur de barres verticales (sans scintillement)
// Chaque barre ne redessine que les cases qui changent depuis l'image precedente.
// Fond noir : les cases eteintes ne sont pas dessinees.
#define MAX_BARS 128
Bar barList[MAX_BARS];
int nBars = 0;
const int SEG = 2;          // hauteur d'une case : 1 ligne allumee + 1 ligne d'espace
const float SPEC_BOOST = 4; // dB : le haut de l'echelle est atteint par les sons forts, pas seulement par les cretes
int segCount(const Bar &br) { return br.center ? H / 4 : H / SEG; }
uint16_t barCellColor(const Bar &br, int seg, bool lit) {
  if (!lit) return 0;
  float f = (seg + 0.5f) / segCount(br);
  return br.center ? hsv(br.hue, 1, 0.35f + 0.65f * f) : hsv(120 - 120 * f, 1, 1);
}
void barFillSeg(const Bar &br, int seg, uint16_t col) {
  if (br.center) {  // depuis le milieu vers le haut et vers le bas
    gfx->fillRect(br.x, H / 2 - 2 - seg * SEG, br.w, 1, col);
    gfx->fillRect(br.x, H / 2 + 1 + seg * SEG, br.w, 1, col);
  } else {
    gfx->fillRect(br.x, H - 1 - seg * SEG, br.w, 1, col);
  }
}
void barDrawSeg(const Bar &br, int seg, bool lit) { barFillSeg(br, seg, barCellColor(br, seg, lit)); }
void barsFullDraw() {
  gfx->fillScreen(0);
  for (int i = 0; i < nBars; i++) {
    barList[i].shown = 0;
    barList[i].cap = barList[i].cap2 = -1;
  }
}
// stereoSplit : gauche / droite de part et d'autre du centre ; center : barres depuis le milieu ;
// gap : espace entre barres (0 = barres collees) ; dual : cretes gauche (rouge) et droite (blanc)
void barsSetup(bool stereoSplit, bool center, int gap, bool dual) {
  int barW = gap ? 3 : max(1, W / NBANDS), pitch = barW + gap;
  nBars = 0;
  if (!stereoSplit) {
    int n = min(MAX_BARS, W / pitch), x0 = (W - n * pitch + gap) / 2;
    for (int i = 0; i < n; i++) {
      Bar br = {};
      br.x = x0 + i * pitch; br.w = barW; br.ch = 2;
      br.band0 = i * NBANDS / n; br.band1 = (i + 1) * NBANDS / n;
      br.center = center; br.hue = i * 300.0f / n; br.dual = dual;
      barList[nBars++] = br;
    }
  } else {  // gauche : graves au centre vers la gauche ; droite : graves au centre vers la droite
    int half = W / 2, n = min(MAX_BARS / 2, half / pitch);
    for (int i = 0; i < n; i++) {
      Bar br = {};
      br.w = barW; br.band0 = i * NBANDS / n; br.band1 = (i + 1) * NBANDS / n;
      br.center = center; br.hue = 200.0f - 200.0f * i / n;
      br.x = half - (i + 1) * pitch + gap; br.ch = 0;
      barList[nBars++] = br;
      br.x = half + i * pitch; br.ch = 1;
      barList[nBars++] = br;
    }
  }
  barsFullDraw();
}
int peakSeg(const Ballistic &b, int n) { return b.peak < 0.02f ? -1 : min(n - 1, (int)(b.peak * n)); }
void barsUpdate() {
  const uint16_t RED = rgb(255, 30, 20), WHITE = rgb(255, 255, 255), YELLOW = rgb(255, 220, 0);
  for (int i = 0; i < nBars; i++) {
    Bar &br = barList[i];
    float db = -120, dbc[2] = { -120, -120 };
    for (int b = br.band0; b < max(br.band0 + 1, (int)br.band1); b++)
      for (int c = 0; c < 2; c++) {
        dbc[c] = max(dbc[c], view.bandDb[c][b]);
        if (br.ch == 2 || br.ch == c) db = max(db, view.bandDb[c][b]);
      }
    br.b.update(norm01(db + SPEC_BOOST));
    int n = segCount(br), lit = (int)(br.b.v * n + 0.5f);
    int cap, cap2 = -1;  // cap2 : crete droite en mode double
    if (br.dual) {
      for (int c = 0; c < 2; c++) br.bc[c].update(norm01(dbc[c] + SPEC_BOOST));
      cap = peakSeg(br.bc[0], n);
      cap2 = peakSeg(br.bc[1], n);
    } else {
      cap = peakSeg(br.b, n);
      if (cap < lit) cap = -1;  // crete dans la barre : invisible
    }
    // efface les anciennes cretes qui bougent, puis met a jour la barre, puis dessine les cretes
    if (br.cap >= 0 && br.cap != cap && br.cap != cap2) barDrawSeg(br, br.cap, br.cap < br.shown);
    if (br.cap2 >= 0 && br.cap2 != cap && br.cap2 != cap2) barDrawSeg(br, br.cap2, br.cap2 < br.shown);
    for (int s = min(lit, (int)br.shown); s < max(lit, (int)br.shown); s++) barDrawSeg(br, s, s < lit);
    if (br.dual) {
      if (cap >= 0 && cap == cap2) barFillSeg(br, cap, YELLOW);  // meme niveau sur les deux voies
      else {
        if (cap >= 0) barFillSeg(br, cap, RED);     // gauche
        if (cap2 >= 0) barFillSeg(br, cap2, WHITE); // droite
      }
    } else if (cap >= 0) {
      barFillSeg(br, cap, WHITE);
    }
    br.shown = lit;
    br.cap = cap;
    br.cap2 = cap2;
  }
}

// ---------------------------------------------------------------- mode Aiguilles : deux vu-metres analogiques
GFXcanvas16 *dialCv = nullptr;
int dialW, dialH;
float needle[2] = { 0, 0 }, needleV[2] = { 0, 0 };
float vuPos(float dbVU) {  // loi des vu-metres : -20 VU a gauche, +3 VU a droite
  float lin = powf(10, dbVU / 20), lo = powf(10, -20 / 20.0f), hi = powf(10, 3 / 20.0f);
  return constrain((lin - lo) / (hi - lo), 0.0f, 1.0f);
}
void needlesSetup() {
  gfx->fillScreen(0);
  if (W >= 128 && H <= 64) { dialW = W / 2; dialH = H; } else { dialW = W; dialH = H / 2; }
  if (dialCv) delete dialCv;
  dialCv = new GFXcanvas16(dialW, dialH);
}
void drawDial(int c) {
  GFXcanvas16 &cv = *dialCv;
  uint16_t face = rgb(20, 70, 200), ink = rgb(10, 6, 0), red = rgb(200, 20, 10);
  cv.fillScreen(face);
  cv.drawRect(0, 0, dialW, dialH, rgb(6, 20, 70));
  float px = dialW / 2.0f, py = dialH * 1.35f, r = dialH * 1.15f;  // pivot sous le cadran
  const float A0 = -0.85f, A1 = 0.85f;                              // angles extremes (radians)
  static const int8_t TICKS[11] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
  for (int t = 0; t < 11; t++) {  // graduations
    float a = A0 + (A1 - A0) * vuPos(TICKS[t]);
    int len = (TICKS[t] == 0 || TICKS[t] == -10 || TICKS[t] == -20 || TICKS[t] == 3) ? 4 : 2;
    for (int k = 0; k < len; k++)
      cv.drawPixel(px + sinf(a) * (r - k), py - cosf(a) * (r - k), TICKS[t] > 0 ? red : ink);
  }
  for (float p = 0; p <= 1.0f; p += 0.01f) {  // arc, rouge au-dessus de 0 VU
    float a = A0 + (A1 - A0) * p;
    cv.drawPixel(px + sinf(a) * (r + 1), py - cosf(a) * (r + 1), p >= vuPos(0) ? red : ink);
  }
  cv.setFont(NULL);
  cv.setTextColor(ink);
  cv.setCursor(dialW / 2 - 5, dialH - 10);
  cv.print("VU");
  cv.setCursor(2, dialH - 9);
  cv.print(c ? "R" : "L");
  float a = A0 + (A1 - A0) * needle[c];
  cv.drawLine(px, py, px + sinf(a) * (r - 1), py - cosf(a) * (r - 1), ink);  // aiguille fine : 1 pixel
  int ox = dialW < W ? c * dialW : 0, oy = dialW < W ? 0 : c * dialH;
  gfx->drawRGBBitmap(ox, oy, cv.getBuffer(), dialW, dialH);
}
void needlesUpdate() {
  if (!dialCv || !dialCv->getBuffer()) return;
  for (int c = 0; c < 2; c++) {
    // 0 VU a 8 dB sous le haut de l'echelle automatique
    float target = view.rmsDb[c] < GATE_DB ? 0 : vuPos(view.rmsDb[c] - (agcRef - 8));
    needleV[c] += (target - needle[c]) * 0.09f;  // aiguille avec inertie
    needleV[c] *= 0.72f;
    needle[c] = constrain(needle[c] + needleV[c], -0.02f, 1.02f);
    drawDial(c);
  }
}

// ---------------------------------------------------------------- modes Puissance et Watts : cadrans d'ampli hi-fi
// Puissance (style 0) : cadran noir retroeclaire en bleu par le bas, eventail de rayons blancs,
//                       aiguille rouge, voyant PEAK blanc au centre.
// Watts (style 1)     : cadran bleu clair, bande d'echelle bleu marine en arc (watts au-dessus,
//                       decibels en dessous), aiguille bleu marine.
// Le fond du cadran est dessine une fois dans une image en memoire puis affiche ; ensuite seule
// l'aiguille bouge : ses anciens pixels sont repris dans l'image du fond (rapide, sans scintillement).
GFXcanvas16 *ampBg = nullptr;
int ampW = 0, ampH = 0, ampStyle = 0, peakHold = 0;
float ampPx = 0, ampPy = 0, ampR = 0;
int ampLast[2][4];  // derniere aiguille dessinee (x0, y0, x1, y1) par cadran
bool ampHasLast[2] = { false, false };
const float AMP_A0 = -0.8f, AMP_A1 = 0.8f;  // angles extremes de l'aiguille (radians)
// echelles : dB (0 = niveau nominal) -> position 0..1 sur le cadran
const int8_t PWR_DB[7] = { -40, -30, -20, -10, -5, 0, 3 };
const float PWR_POS[7] = { 0, 0.17f, 0.40f, 0.66f, 0.78f, 0.90f, 1.0f };
const int8_t WAT_DB[8] = { -60, -50, -40, -30, -20, -10, 0, 5 };
const float WAT_POS[8] = { 0, 0.10f, 0.25f, 0.38f, 0.50f, 0.66f, 0.85f, 1.0f };
float scalePos(const int8_t *db, const float *pos, int n, float v) {
  if (v <= db[0]) return 0;
  for (int i = 1; i < n; i++)
    if (v <= db[i]) return pos[i - 1] + (pos[i] - pos[i - 1]) * (v - db[i - 1]) / (db[i] - db[i - 1]);
  return 1;
}
float ampPos(float db) { return ampStyle ? scalePos(WAT_DB, WAT_POS, 8, db) : scalePos(PWR_DB, PWR_POS, 7, db); }

// petite police 3x5 (chiffres et quelques lettres) pour les graduations
const uint8_t TF_DIG[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
                                { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
const uint8_t TF_L[5] = { 4, 4, 4, 4, 7 }, TF_R[5] = { 6, 5, 6, 5, 5 }, TF_P[5] = { 6, 5, 6, 4, 4 }, TF_E[5] = { 7, 4, 6, 4, 7 },
              TF_A[5] = { 2, 5, 7, 5, 5 }, TF_K[5] = { 5, 5, 6, 5, 5 }, TF_M[5] = { 0, 6, 7, 5, 5 }, TF_DOT[5] = { 0, 0, 0, 0, 2 },
              TF_S[5] = { 7, 4, 7, 1, 7 }, TF_C[5] = { 7, 4, 4, 4, 7 }, TF_D[5] = { 6, 5, 5, 5, 6 };
const uint8_t *tfGlyph(char c) {
  if (c >= '0' && c <= '9') return TF_DIG[c - '0'];
  switch (c) {
    case 'L': return TF_L; case 'R': return TF_R; case 'P': return TF_P; case 'E': return TF_E;
    case 'A': return TF_A; case 'K': return TF_K; case 'm': return TF_M; case '.': return TF_DOT;
    case 'S': return TF_S; case 'C': return TF_C; case 'O': return TF_DIG[0]; case 'D': return TF_D;
  }
  return nullptr;
}
int tinyWidth(const char *s) { return strlen(s) * 4 - 1; }
void tinyText(Adafruit_GFX &g, int x, int y, const char *s, uint16_t col) {
  for (; *s; s++, x += 4) {
    const uint8_t *gl = tfGlyph(*s);
    if (!gl) continue;
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 3; c++)
        if (gl[r] & (4 >> c)) g.drawPixel(x + c, y + r, col);
  }
}
// texte centre sur un point de l'arc (rayon rr, position p de 0 a 1), garde dans le cadran
void ampLabel(GFXcanvas16 &cv, float p, float rr, const char *s, uint16_t col) {
  float a = AMP_A0 + (AMP_A1 - AMP_A0) * p;
  int x = constrain((int)(ampPx + sinf(a) * rr - tinyWidth(s) / 2.0f), 1, ampW - 1 - tinyWidth(s)), y = ampPy - cosf(a) * rr - 2;
  tinyText(cv, x, y, s, col);
}
void ampSpoke(GFXcanvas16 &cv, float p, float r0, float r1, uint16_t col) {
  float a = AMP_A0 + (AMP_A1 - AMP_A0) * p, s = sinf(a), c = cosf(a);
  cv.drawLine(ampPx + s * r0, ampPy - c * r0, ampPx + s * r1, ampPy - c * r1, col);
}
void ampFree() {
  if (ampBg) { delete ampBg; ampBg = nullptr; }
}
bool ampSideBySide() { return W >= 128 && H <= 64; }  // sinon les deux cadrans sont l'un sous l'autre
int ampOx(int c) { return ampSideBySide() ? c * ampW : 0; }
int ampOy(int c) { return ampSideBySide() ? 0 : c * ampH; }
// trace (col >= 0) ou efface (col < 0 : pixels repris du fond) une aiguille, pixel par pixel
void ampLine(int c, int x0, int y0, int x1, int y1, int32_t col) {
  uint16_t *bg = ampBg->getBuffer();
  int ox = ampOx(c), oy = ampOy(c);
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    if (x0 >= 0 && x0 < ampW && y0 >= 0 && y0 < ampH) gfx->drawPixel(ox + x0, oy + y0, col >= 0 ? (uint16_t)col : bg[y0 * ampW + x0]);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void ampSetup(int style) {
  gfx->fillScreen(0);
  ampStyle = style;
  if (ampSideBySide()) { ampW = W / 2; ampH = H; } else { ampW = W; ampH = H / 2; }
  ampFree();
  ampBg = new GFXcanvas16(ampW, ampH);
  if (!ampBg->getBuffer()) {
    ampFree();
    Serial.printf("Memoire insuffisante pour le cadran %dx%d (libre : %u octets)\n", ampW, ampH, ESP.getFreeHeap());
    return;
  }
  ampHasLast[0] = ampHasLast[1] = false;
  bool big = ampH >= 48;  // place pour les graduations chiffrees
  ampR = min(ampH * 1.12f, (ampW / 2 - 2) / sinf(AMP_A1));
  ampPx = ampW / 2.0f;
  ampPy = (big ? 14 : 6) + ampR;
  GFXcanvas16 &cv = *ampBg;
  if (style == 0) {
    for (int y = 0; y < ampH; y++) {  // noir en haut, lueur bleue en bas (eclairage du cadran)
      float f = (float)y / ampH, g = f > 0.5f ? (f - 0.5f) / 0.5f : 0;
      g *= g;
      cv.drawFastHLine(0, y, ampW, rgb(2 + 25 * g, 3 + 60 * g, 12 + 210 * g));
    }
    static const int8_t MINOR[4] = { -35, -25, -15, -7 };
    for (int i = 0; i < 4; i++) ampSpoke(cv, scalePos(PWR_DB, PWR_POS, 7, MINOR[i]), ampR * 0.62f, ampR * 0.92f, rgb(90, 100, 150));
    for (int i = 0; i < 7; i++) {
      float p = PWR_POS[i];
      ampSpoke(cv, p, ampR * 0.42f, ampR, rgb(225, 230, 255));
      if (big) {
        char t[4];
        snprintf(t, sizeof(t), "%d", abs(PWR_DB[i]));
        ampLabel(cv, p, ampR + 5, t, rgb(235, 240, 255));
      }
    }
  } else {
    for (int y = 0; y < ampH; y++) {  // bleu clair, plus lumineux en bas
      float f = (float)y / ampH, g = f > 0.6f ? (f - 0.6f) / 0.4f : 0;
      cv.drawFastHLine(0, y, ampW, rgb(70 + 80 * g, 150 + 60 * g, 245 + 10 * g));
    }
    uint16_t navy = rgb(8, 18, 110);
    int steps = ampR * 4;
    for (int k = 0; k <= steps; k++) {  // bande epaisse, coupee a chaque graduation, plus large a droite
      float p = (float)k / steps;
      bool gap = false;
      for (int i = 1; i < 7; i++) gap |= fabsf(p - WAT_POS[i]) < 0.006f;
      if (gap) continue;
      float a = AMP_A0 + (AMP_A1 - AMP_A0) * p, s = sinf(a), c = cosf(a);
      int thick = p > 0.85f ? 4 : 3;
      for (int t = 0; t < thick; t++) cv.drawPixel(ampPx + s * (ampR - t), ampPy - c * (ampR - t), navy);
    }
    if (big) {
      bool wide = ampW >= 100;
      static const char *WT[6] = { "5.0m", "50m", "100", "150", "200", "250" };
      static const float WP[6] = { 0.05f, 0.22f, 0.40f, 0.53f, 0.67f, 0.84f };
      for (int i = 0; i < 6; i++)
        if (wide || i == 1 || i == 2 || i == 4) ampLabel(cv, WP[i], ampR + 5, WT[i], navy);  // watts au-dessus
      for (int i = 1; i < 7; i++)
        if (wide || i % 2 == 0) {  // decibels en dessous
          char t[4];
          snprintf(t, sizeof(t), "%d", abs(WAT_DB[i]));
          ampLabel(cv, WAT_POS[i], ampR - 9, t, navy);
        }
    }
  }
  for (int c = 0; c < 2; c++) gfx->drawRGBBitmap(ampOx(c), ampOy(c), ampBg->getBuffer(), ampW, ampH);
  if (style == 0 && ampSideBySide()) tinyText(*gfx, W / 2 - 7, H - 18, "PEAK", rgb(200, 205, 230));
}
void ampUpdate() {
  if (!ampBg) return;
  uint16_t needleCol = ampStyle ? rgb(8, 18, 110) : rgb(255, 40, 30);
  for (int c = 0; c < 2; c++) {
    float target = view.rmsDb[c] < GATE_DB ? 0 : ampPos(view.rmsDb[c] - (agcRef - 8));
    needleV[c] += (target - needle[c]) * 0.09f;  // aiguille avec inertie
    needleV[c] *= 0.72f;
    needle[c] = constrain(needle[c] + needleV[c], -0.02f, 1.02f);
    float a = AMP_A0 + (AMP_A1 - AMP_A0) * needle[c];
    int x0 = ampPx, y0 = ampPy, x1 = ampPx + sinf(a) * (ampR + 3), y1 = ampPy - cosf(a) * (ampR + 3);
    int *last = ampLast[c];
    if (ampHasLast[c] && last[2] == x1 && last[3] == y1) continue;  // aiguille immobile
    if (ampHasLast[c]) ampLine(c, last[0], last[1], last[2], last[3], -1);  // efface l'ancienne
    ampLine(c, x0, y0, x1, y1, needleCol);
    last[0] = x0; last[1] = y0; last[2] = x1; last[3] = y1;
    ampHasLast[c] = true;
    tinyText(*gfx, ampOx(c) + (c ? ampW - 5 : 2), ampOy(c) + ampH - 6, c ? "R" : "L", ampStyle ? rgb(8, 18, 110) : rgb(150, 160, 200));
  }
  if (ampStyle == 0) {  // voyant PEAK
    if (max(view.peakDb[0], view.peakDb[1]) > agcRef + 1) peakHold = 12;
    else if (peakHold > 0) peakHold--;
    uint16_t led = peakHold ? rgb(230, 240, 255) : rgb(25, 30, 50);
    int lx = ampSideBySide() ? W / 2 : W - 6, ly = ampSideBySide() ? H - 8 : H / 2;
    gfx->drawCircle(lx, ly, 3, peakHold ? rgb(40, 80, 220) : rgb(16, 37, 130));
    gfx->fillCircle(lx, ly, 2, led);
  }
}

// ---------------------------------------------------------------- mode Jeu : shoot'em up d'arcade qui joue tout seul
// Sprites originaux multicolores (9x7), formation qui oscille, ennemis qui piquent en courbe,
// ecran d'arcade : SCORE et RECORD en haut, vies et vague en bas (128x128).
// Reagit a la musique : ennemis plus lumineux sur les basses, etoiles plus rapides quand le son monte.
// Dessin sans scintillement : seuls les rectangles dessines a l'image precedente sont effaces.
#define G_MAXE 60
#define G_SHOTS 3
#define G_BOMBS 10
#define G_BOOMS 10
#define G_STARS 64
#define G_RECTS 260
#define G_MAXROWS 5
const int GW = 9, GH = 7;  // taille des ennemis
struct GEnemy { float x, y, vx, vy; int16_t t; int8_t row, col, type, hp, dir; uint8_t state; bool alive; };  // state : 0 rang, 1 pique, 2 retour
struct GDot { float x, y; bool on; };
struct GBoom { int16_t x, y; uint8_t t; };
struct GStar { int16_t x; float y, v; uint8_t hue; };
struct GRect { int16_t x, y; uint8_t w, h; };
GEnemy gEn[G_MAXE];
GDot gShot[G_SHOTS], gBomb[G_BOMBS];
GBoom gBoom[G_BOOMS];
GStar gStar[G_STARS];
GRect gOld[G_RECTS];
int gNE = 0, gCols = 4, gRows = 3, gTop = 13, gPY = 49, gNStar = 0, gNOld = 0, gLives = 0, gWave = 0, gDead = 0, gCool = 0;
uint32_t gFrame = 0, gScore = 0, gHi = 0, gLastStep = 0;
float gPx = 0, gSway = 0;
int8_t gRowType[G_MAXROWS];

// sprites : '.' transparent, sinon une lettre de la palette ci-dessous
// type 0 : meduse (2 tirs), type 1 : scarabee, type 2 : insecte ailé
const char *const SPR_E[3][2][GH] = {
  { { "..ppppp..", ".pwpppwp.", "ppppppppp", "p.c.c.c.p", "..c.c.c..", ".c..c..c.", "c...c...c" },
    { "..ppppp..", ".pwpppwp.", "ppppppppp", "p.c.c.c.p", ".c..c..c.", "..c.c.c..", "...c.c..." } },
  { { "v...o...v", ".v.ooo.v.", "..oowoo..", "vvooooovv", "v.ooooo.v", "..o.o.o..", ".o.....o." },
    { ".v..o..v.", "..vooov..", "..oowoo..", ".vooooov.", "v.ooooo.v", ".o..o..o.", "o.......o" } },
  { { "c.......c", ".c.ggg.c.", "..gwywg..", "cgyyyyygc", "c.ggggg.c", "..g.y.g..", "...g.g..." },
    { "cc.....cc", "..cgggc..", "..gwywg..", ".gyyyyyg.", "..ggggg..", "..g.y.g..", "...g.g..." } }
};
const char *const SPR_SHIP[8] = { "....c....", "...ccc...", "...cwc...", "..bcwcb..", ".bbcccbb.", "bbcbcbcbb", "b.o...o.b", "..o...o.." };
const char *const SPR_LIFE[4] = { "..c..", ".ccc.", "bcwcb", "b...b" };
// palette : w blanc, r rouge, y jaune, b bleu, c cyan, g vert, p magenta, o orange, v violet
const char G_PAL_CH[] = "wrybcgpov";
const uint8_t G_PAL_RGB[9][3] = { { 255, 255, 255 }, { 255, 40, 40 }, { 255, 220, 0 }, { 40, 90, 255 }, { 0, 220, 255 },
                                  { 40, 230, 80 }, { 255, 60, 200 }, { 255, 140, 0 }, { 150, 60, 255 } };
uint16_t gPal[9];  // palette de l'image en cours (luminosite qui suit les basses)
int gPalIdx(char ch) {
  for (int i = 0; i < 9; i++)
    if (G_PAL_CH[i] == ch) return i;
  return -1;
}
void gMark(int x, int y, int w, int h) {
  if (gNOld < G_RECTS) gOld[gNOld++] = { (int16_t)x, (int16_t)y, (uint8_t)w, (uint8_t)h };
}
void gPix(int x, int y, uint16_t c) {
  if (x >= 0 && x < W && y >= 0 && y < H) gfx->drawPixel(x, y, c);
  gMark(x, y, 1, 1);
}
// dessine un sprite ; swapFrom -> swapTo permet de changer une couleur (meduse touchee)
void gSprite(const char *const *rows, int w, int h, int x, int y, const uint16_t *pal, char swapFrom, char swapTo) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      char ch = rows[r][c];
      if (ch == '.') continue;
      if (ch == swapFrom) ch = swapTo;
      int i = gPalIdx(ch), px = x + c, py = y + r;
      if (i >= 0 && px >= 0 && px < W && py >= 0 && py < H) gfx->drawPixel(px, py, pal[i]);
    }
  gMark(x, y, w, h);
}
float gSlotX(int col) { return (W - gCols * 11) / 2 + 1 + col * 11 + gSway; }
float gSlotY(int row) { return gTop + row * 9; }
void gBoomAt(int x, int y) {
  for (int i = 0; i < G_BOOMS; i++)
    if (gBoom[i].t == 0) { gBoom[i] = { (int16_t)x, (int16_t)y, 1 }; return; }
}
void gNewWave() {
  gCols = constrain((W - 6) / 11, 4, 11);
  gNE = 0;
  for (int r = 0; r < gRows; r++)
    for (int c = 0; c < gCols && gNE < G_MAXE; c++) {
      if (gRowType[r] == 0 && (c & 1)) continue;  // meduses une place sur deux
      GEnemy &e = gEn[gNE++];
      e = {};
      e.row = r; e.col = c; e.type = gRowType[r]; e.alive = true;
      e.hp = e.type == 0 ? 2 : 1;
      e.x = gSlotX(c); e.y = -10 - 8 * (gRows - r);  // arrivent du haut
      e.state = 2;
    }
  gWave++;
}
void gReset() {
  gLives = 3; gScore = 0; gWave = 0; gDead = 0; gCool = 0;
  gPx = W / 2 - 4;
  for (auto &s : gShot) s.on = false;
  for (auto &b : gBomb) b.on = false;
  for (auto &b : gBoom) b.t = 0;
  gNewWave();
}
void gameSetup() {
  gfx->fillScreen(0);
  gNOld = 0;
  bool tall = H >= 128;
  gRows = tall ? 5 : 3;
  static const int8_t T5[5] = { 0, 1, 1, 2, 2 }, T3[3] = { 0, 1, 2 };
  for (int r = 0; r < gRows; r++) gRowType[r] = tall ? T5[r] : T3[r];
  gTop = 13;
  gPY = tall ? H - 15 : H - 8;  // sur 128x128 : la ligne du bas montre vies et vague
  gHi = prefs.getUInt("hiscore", 0);
  if (gLives <= 0 && gDead == 0) gReset();
  gNStar = min(G_STARS, W * H / 220);
  for (int i = 0; i < gNStar; i++) gStar[i] = { (int16_t)random(W), (float)random(12, H), 0.2f + random(100) / 120.0f, (uint8_t)random(9) };
  gLastStep = millis();
}
void gHitPlayer() {
  gBoomAt(gPx + 4, gPY + 3);
  gLives--;
  gDead = gLives > 0 ? 50 : 120;  // pause avant le vaisseau suivant (plus longue apres GAME OVER)
  if (gLives <= 0 && gScore > gHi) { gHi = gScore; prefs.putUInt("hiscore", gHi); }  // nouveau record
}
void gameStep() {
  gFrame++;
  float loud = view.rmsDb[0] < GATE_DB ? 0 : norm01(max(view.rmsDb[0], view.rmsDb[1]) + 6);
  for (int i = 0; i < gNStar; i++) {  // etoiles
    GStar &s = gStar[i];
    s.y += s.v * (0.4f + 2.5f * loud);
    if (s.y >= H) { s.y = 12; s.x = random(W); }
  }
  // formation et piques
  gSway = sinf(gFrame * 0.03f) * max(1, min(6, (W - gCols * 11) / 2 - 1));
  int alive = 0;
  for (int i = 0; i < gNE; i++) alive += gEn[i].alive;
  int diveEvery = max(16, 60 - gWave * 7);
  if (gDead == 0 && gFrame % diveEvery == 0 && alive) {
    int k = random(gNE);
    for (int t = 0; t < gNE; t++, k = (k + 1) % gNE)
      if (gEn[k].alive && gEn[k].state == 0) {
        GEnemy &e = gEn[k];
        e.state = 1; e.vx = 0; e.vy = -0.6f; e.t = 0;
        e.dir = e.x < W / 2 ? -1 : 1;  // part en boucle vers l'exterieur
        break;
      }
  }
  float shipC = gPx + 4;
  for (int i = 0; i < gNE; i++) {
    GEnemy &e = gEn[i];
    if (!e.alive) continue;
    float sx = gSlotX(e.col), sy = gSlotY(e.row);
    if (e.state == 0) {
      e.x = sx; e.y = sy;
    } else if (e.state == 1) {  // pique en courbe : boucle puis descente en visant le vaisseau
      e.t++;
      float curve = e.t < 50 ? e.dir * 1.2f * cosf(e.t * 0.063f) : 0;
      e.vx = constrain(e.vx * 0.96f + curve * 0.06f + (shipC - (e.x + 4)) * (e.t < 50 ? 0.0005f : 0.004f), -1.4f, 1.4f);
      e.vy = min(e.vy + 0.03f, 1.3f + 0.1f * gWave);
      e.x += e.vx + curve; e.y += e.vy;
      if (e.y > H) { e.y = -8; e.state = 2; }
    } else {  // retour a sa place
      float dx = sx - e.x, dy = sy - e.y;
      e.x += constrain(dx * 0.08f, -1.5f, 1.5f);
      e.y += constrain(dy * 0.08f, -1.5f, 1.5f);
      if (fabsf(dx) < 0.6f && fabsf(dy) < 0.6f) e.state = 0;
    }
    int chance = e.state == 1 ? 18 : 2 + gWave;  // tirs ennemis
    if (gDead == 0 && e.y > 0 && e.y < H * 0.6f && random(1000) < chance)
      for (auto &b : gBomb)
        if (!b.on) { b = { e.x + 4, e.y + GH, true }; break; }
    if (gDead == 0 && e.state == 1 && e.x + GW - 1 >= gPx && e.x <= gPx + 8 && e.y + GH - 1 >= gPY && e.y <= gPY + 7) {
      e.alive = false;  // collision avec le vaisseau
      gBoomAt(e.x + 4, e.y + 3);
      gHitPlayer();
    }
  }
  // pilote automatique : esquive les tirs, sinon vise un ennemi en pique ou la colonne la plus proche
  if (gDead == 0) {
    float target = gPx;
    bool dodge = false;
    for (auto &b : gBomb)
      if (b.on && b.y > gPY - 24 && fabsf(b.x - shipC) < 6) { target = b.x < shipC ? gPx + 7 : gPx - 7; dodge = true; break; }
    if (!dodge) {
      float best = 1e9;
      for (int i = 0; i < gNE; i++) {
        GEnemy &e = gEn[i];
        if (!e.alive || e.y < 0) continue;
        float d = fabsf(e.x - gPx) - (e.state == 1 ? 40 : 0) - (e.type == 0 ? 8 : 0);  // priorite : piques, meduses
        if (d < best) { best = d; target = e.x; }
      }
    }
    gPx += constrain(target - gPx, -1.3f, 1.3f);
    gPx = constrain(gPx, 0.0f, (float)(W - GW));
    if (gCool > 0) gCool--;
    if (gCool == 0)  // tir quand un ennemi est au-dessus
      for (int i = 0; i < gNE; i++)
        if (gEn[i].alive && gEn[i].y > 0 && fabsf(gEn[i].x - gPx) < 4) {
          for (auto &s : gShot)
            if (!s.on) { s = { gPx + 4, (float)gPY - 3, true }; gCool = 7; break; }
          break;
        }
  } else if (--gDead == 0 && gLives <= 0) {
    gReset();  // fin de partie : on recommence
  }
  for (auto &s : gShot) {  // tirs du vaisseau
    if (!s.on) continue;
    s.y -= 3;
    if (s.y < 0) { s.on = false; continue; }
    for (int i = 0; i < gNE; i++) {
      GEnemy &e = gEn[i];
      if (e.alive && s.x >= e.x && s.x <= e.x + GW - 1 && s.y <= e.y + GH - 1 && s.y + 2 >= e.y) {
        s.on = false;
        if (--e.hp > 0) break;  // meduse : il faut 2 tirs
        e.alive = false;
        static const uint16_t PTS[3][2] = { { 150, 400 }, { 80, 160 }, { 50, 100 } };
        gScore += PTS[e.type][e.state == 1];
        if (gScore > gHi) gHi = gScore;
        gBoomAt(e.x + 4, e.y + 3);
        break;
      }
    }
  }
  for (auto &b : gBomb) {  // tirs ennemis
    if (!b.on) continue;
    b.y += 1.3f + 0.1f * gWave;
    if (b.y >= H) { b.on = false; continue; }
    if (gDead == 0 && b.x >= gPx && b.x <= gPx + 8 && b.y + 2 >= gPY + 2 && b.y <= gPY + 7) { b.on = false; gHitPlayer(); }
  }
  for (auto &b : gBoom)
    if (b.t) b.t = b.t >= 12 ? 0 : b.t + 1;
  bool any = false;
  for (int i = 0; i < gNE; i++) any |= gEn[i].alive;
  if (!any) gNewWave();
}
void gameDraw() {
  for (int i = 0; i < gNOld; i++) {  // efface l'image precedente
    GRect &r = gOld[i];
    int x0 = max(0, (int)r.x), y0 = max(0, (int)r.y), x1 = min(W, r.x + r.w), y1 = min(H, r.y + r.h);
    if (x1 > x0 && y1 > y0) gfx->fillRect(x0, y0, x1 - x0, y1 - y0, 0);
  }
  gNOld = 0;
  float bass = -120;
  for (int b = 0; b < 6; b++) bass = max(bass, max(view.bandDb[0][b], view.bandDb[1][b]));
  float glow = 0.55f + 0.45f * norm01(bass + SPEC_BOOST);
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) {
    gPal[i] = rgb(G_PAL_RGB[i][0] * glow, G_PAL_RGB[i][1] * glow, G_PAL_RGB[i][2] * glow);
    palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  }
  for (int i = 0; i < gNStar; i++) {  // etoiles de couleur qui scintillent
    GStar &s = gStar[i];
    if (((gFrame + i * 7) & 31) < 3) continue;
    const uint8_t *c = G_PAL_RGB[s.hue];
    float k = 0.15f + s.v * 0.3f;
    gPix(s.x, (int)s.y, rgb(c[0] * k, c[1] * k, c[2] * k));
  }
  for (int i = 0; i < gNE; i++) {
    GEnemy &e = gEn[i];
    if (!e.alive) continue;
    int fr = e.state == 1 ? (gFrame / 5) & 1 : ((gFrame / 16) + e.col) & 1;  // animation
    gSprite(SPR_E[e.type][fr], GW, GH, (int)e.x, (int)e.y, gPal, 'p', e.hp == 1 && e.type == 0 ? 'o' : 'p');
  }
  for (auto &s : gShot)
    if (s.on) { gPix(s.x, s.y, palFull[0]); gPix(s.x, s.y + 1, palFull[2]); gPix(s.x, s.y + 2, palFull[7]); }
  for (auto &b : gBomb)
    if (b.on) { gPix(b.x, b.y, palFull[1]); gPix(b.x, b.y + 1, palFull[1]); gPix(b.x, b.y + 2, palFull[0]); }
  static const int8_t DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
  for (auto &b : gBoom) {  // explosions : anneau qui grandit, du blanc au rouge
    if (!b.t) continue;
    uint16_t c = b.t < 4 ? palFull[0] : b.t < 7 ? palFull[2] : b.t < 10 ? palFull[7] : palFull[1];
    for (int k = 0; k < 8; k++) gPix(b.x + DX[k] * b.t / 2, b.y + DY[k] * b.t / 2, c);
    if (b.t < 6) gPix(b.x, b.y, palFull[0]);
  }
  if (gDead == 0) gSprite(SPR_SHIP, 9, 8, (int)gPx, gPY, palFull, 0, 0);
  // ecran d'arcade : SCORE a gauche, RECORD au centre
  char sc[12];
  uint16_t red = rgb(255, 40, 40), white = rgb(230, 230, 230);
  tinyText(*gfx, 1, 0, "SCORE", red);
  snprintf(sc, sizeof(sc), "%lu", (unsigned long)gScore);
  tinyText(*gfx, 1, 6, sc, white);
  gMark(1, 6, tinyWidth(sc), 5);
  int rx = W >= 96 ? W / 2 - 11 : W - 24;
  tinyText(*gfx, rx, 0, "RECORD", red);
  snprintf(sc, sizeof(sc), "%lu", (unsigned long)gHi);
  tinyText(*gfx, rx, 6, sc, white);
  gMark(rx, 6, tinyWidth(sc), 5);
  if (H >= 128) {  // ligne du bas : vies a gauche, vague a droite
    for (int i = 0; i < gLives - 1 && i < 6; i++) gSprite(SPR_LIFE, 5, 4, 1 + i * 7, H - 5, palFull, 0, 0);
    for (int i = 0; i < gWave && i < 10; i++) {
      int x = W - 4 - i * 4;
      gPix(x, H - 5, palFull[2]); gPix(x + 1, H - 5, palFull[2]);
      gPix(x, H - 4, palFull[1]); gPix(x + 1, H - 4, palFull[1]);
      gPix(x, H - 3, palFull[1]); gPix(x + 1, H - 3, palFull[1]);
      gPix(x, H - 2, palFull[3]);
    }
  } else if (W >= 96) {  // ecran bas : vies en haut a droite
    for (int i = 0; i < gLives - 1 && i < 4; i++) gSprite(SPR_LIFE, 5, 4, W - 6 - i * 7, 1, palFull, 0, 0);
  }
  if (gLives <= 0 && gDead > 0) {
    gfx->setFont(NULL);
    gfx->setTextSize(1);
    gfx->setTextColor(red);
    gfx->setCursor(W / 2 - 27, H / 2 - 4);
    gfx->print("GAME OVER");
    gMark(W / 2 - 27, H / 2 - 4, 54, 8);
  }
}
void gameUpdate() {
  uint32_t now = millis();
  if (now - gLastStep < 25) return;  // 40 images par seconde
  gLastStep = now - gLastStep > 100 ? now : gLastStep + 25;
  gameStep();
  gameDraw();
}

// ---------------------------------------------------------------- choix du mode
uint8_t shownMode = 255;
uint32_t autoSince = 0;
uint8_t autoMode = M_SPECTRUM;
void enterMode(uint8_t m) {
  if (dialCv && m != M_NEEDLES) { delete dialCv; dialCv = nullptr; }
  if (m != M_POWER && m != M_WATTS) ampFree();
  switch (m) {
    case M_SPECTRUM: barsSetup(false, false, 1, false); break;
    case M_MIRROR: barsSetup(false, true, 1, false); break;
    case M_NEEDLES: needlesSetup(); break;
    case M_STEREO: barsSetup(true, false, 1, false); break;
    case M_GAME: gameSetup(); break;
    case M_POWER: ampSetup(0); break;
    case M_WATTS: ampSetup(1); break;
    case M_SPECTRUM_ST: barsSetup(false, false, 1, true); break;
  }
  shownMode = m;
}
void updateMode(uint8_t m) {
  switch (m) {
    case M_SPECTRUM: case M_MIRROR: case M_STEREO: case M_SPECTRUM_ST: barsUpdate(); break;
    case M_NEEDLES: needlesUpdate(); break;
    case M_GAME: gameUpdate(); break;
    case M_POWER: case M_WATTS: ampUpdate(); break;
  }
}

// message de 1,5 s en haut a gauche (nom du mode, luminosite...)
char overlay[24] = "";
uint32_t overlayUntil = 0;
void showMessage(const char *msg) {
  strncpy(overlay, msg, sizeof(overlay) - 1);
  overlayUntil = millis() + 1500;
}
void drawOverlay() {
  static bool shown = false;
  if (millis() < overlayUntil) {
    int w = strlen(overlay) * 6 + 3;
    gfx->fillRect(0, 0, w, 10, 0);
    gfx->drawRect(0, 0, w, 10, rgb(80, 80, 80));
    gfx->setFont(NULL);
    gfx->setTextSize(1);
    gfx->setTextColor(rgb(255, 255, 255));
    gfx->setCursor(2, 1);
    gfx->print(overlay);
    shown = true;
  } else if (shown) {  // message parti : ecran redessine
    shown = false;
    enterMode(shownMode);
  }
}

// ---------------------------------------------------------------- reglages communs (bouton et moniteur serie)
void nextMode() {
  mode = (mode + 1) % M_COUNT;
  prefs.putUChar("mode", mode);
  showMessage(MODE_NAME[mode]);
  autoSince = millis();
}
void nextBright() {
  brightIdx = (brightIdx + 1) % 5;
  dma->setBrightness8(BRIGHT[brightIdx]);
  prefs.putUChar("bright", brightIdx);
  char m[24];
  snprintf(m, sizeof(m), "Lum %d/5", brightIdx + 1);
  showMessage(m);
}
void applyLayout(uint8_t l) {  // enregistre la dalle et redemarre
  char m[24];
  snprintf(m, sizeof(m), "Dalle %s", LAYOUT_NAME[l]);
  showMessage(m);
  drawOverlay();
  prefs.putUChar("layout", l);
  Serial.printf("Dalle %s enregistree, redemarrage...\n", LAYOUT_NAME[l]);
  Serial.flush();
  delay(1500);
  ESP.restart();
}

// ---------------------------------------------------------------- bouton
void handleButton() {
  static uint32_t downAt = 0;
  static bool down = false, layoutShown = false;
  bool pressed = digitalRead(BTN_PIN) == LOW || digitalRead(BOOT_PIN) == LOW;
  uint32_t now = millis();
  if (pressed && !down) { down = true; downAt = now; layoutShown = false; }
  if (pressed && down && now - downAt > 5000 && !layoutShown) {  // 5 s : dalle suivante
    layoutShown = true;
    applyLayout((layout + 1) % N_LAYOUTS);
  }
  if (!pressed && down) {
    down = false;
    uint32_t d = now - downAt;
    if (d < 40) return;  // rebond
    if (d < 1000) nextMode();          // court : mode suivant
    else if (d < 5000) nextBright();   // 1 a 5 s : luminosite
  }
}

// ---------------------------------------------------------------- bouton 2 : type de dalle
// Chaque appui avance d'une dalle (affichee) ; 2 s apres le dernier appui : enregistre et redemarre.
void handleLayoutButton() {
  static bool down = false;
  static uint32_t changedAt = 0, pressAt = 0;
  static int8_t pending = -1;
  static bool armed = false;  // ignore le bouton tant qu'il n'a pas ete vu relache (broche en l'air ou bloquee)
  uint32_t now = millis();
  bool pressed = digitalRead(LAYOUT_BTN_PIN) == HIGH;
  if (!armed) { armed = !pressed; return; }
  if (pressed && !down && now - pressAt > 60) {  // nouvel appui (anti-rebond)
    down = true;
    pressAt = now;
    pending = ((pending < 0 ? layout : pending) + 1) % N_LAYOUTS;
    changedAt = now;
    char m[24];
    snprintf(m, sizeof(m), "Dalle %s", LAYOUT_NAME[pending]);
    showMessage(m);
  } else if (!pressed && down && now - pressAt > 60) {
    down = false;
  }
  if (pending >= 0 && !down && now - changedAt > 2000) {
    if (pending != layout) applyLayout(pending);
    pending = -1;  // revenu sur la dalle actuelle : rien a faire
  }
}

// ---------------------------------------------------------------- menu du moniteur serie (115200 bauds)
bool demoMenu = false;  // demo lancee depuis le menu (memorisee), en plus du cavalier GPIO 18
void printMenu() {
  bool jumper = digitalRead(DEMO_PIN) == LOW;
  Serial.println();
  Serial.println("===== VU-metre v" VU_VERSION " =====");
  Serial.printf("Dalle : %s   Mode : %s   Luminosite : %d/5   Memoire libre : %u\n", LAYOUT_NAME[layout], MODE_NAME[mode], brightIdx + 1, ESP.getFreeHeap());
  Serial.printf("Demo  : %s%s\n", demoOn ? "en marche" : "arretee",
                jumper ? " (cavalier GPIO18 en place)" : demoMenu ? " (lancee depuis ce menu)" : "");
  Serial.println("-------------------------------");
  for (int i = 0; i < N_LAYOUTS; i++) Serial.printf("  %d : dalle %s%s\n", i + 1, LAYOUT_NAME[i], i == layout ? "  <- actuelle" : "");
  Serial.println("  d : demo marche / arret");
  Serial.println("  m : mode d'affichage suivant");
  Serial.println("  l : luminosite suivante");
  Serial.println("  ? : afficher ce menu");
  Serial.println("Tapez une touche puis Entree.");
}
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c < '1' + N_LAYOUTS) {
      uint8_t l = c - '1';
      if (l == layout) Serial.printf("Dalle %s deja choisie.\n", LAYOUT_NAME[l]);
      else applyLayout(l);
    } else if (c == 'd' || c == 'D') {
      demoMenu = !demoMenu;
      prefs.putBool("demo", demoMenu);
      Serial.printf("Demo %s (memorise)\n", demoMenu ? "lancee" : "arretee");
      if (!demoMenu && digitalRead(DEMO_PIN) == LOW) Serial.println("Attention : le cavalier GPIO18 garde la demo en marche.");
    } else if (c == 'm' || c == 'M') {
      nextMode();
      Serial.printf("Mode : %s\n", MODE_NAME[mode]);
    } else if (c == 'l' || c == 'L') {
      nextBright();
      Serial.printf("Luminosite : %d/5\n", brightIdx + 1);
    } else if (c == '?' || c == 'h' || c == 'H') {
      printMenu();
    } else if (c > ' ') {
      Serial.printf("Touche '%c' inconnue, tapez ? pour le menu.\n", c);
    }
  }
}

// ---------------------------------------------------------------- cavalier du mode demo (anti-rebond 100 ms)
void handleDemoPin() {
  static uint32_t last = 0;
  static uint8_t same = 0;
  if (millis() - last < 20) return;
  last = millis();
  bool want = digitalRead(DEMO_PIN) == LOW || demoMenu;
  if (want == demoOn) { same = 0; return; }
  if (++same < 5) return;
  same = 0;
  demoOn = want;
  showMessage(want ? "Demo" : "Entree audio");
}

// ---------------------------------------------------------------- demarrage
void setup() {
  Serial.begin(115200);
  Serial.println("VU-metre v" VU_VERSION);
  WiFi.mode(WIFI_OFF);  // pas de wifi : toute la memoire pour la dalle
  btStop();
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(DEMO_PIN, INPUT_PULLUP);
  pinMode(LAYOUT_BTN_PIN, INPUT);  // pas de rappel interne sur GPIO 34
  delay(5);
  prefs.begin("vu", false);
  demoMenu = prefs.getBool("demo", false);
  demoOn = digitalRead(DEMO_PIN) == LOW || demoMenu;
  layout = prefs.getUChar("layout", 0) % N_LAYOUTS;
  mode = prefs.getUChar("mode", M_AUTO) % M_COUNT;
  brightIdx = prefs.getUChar("bright", 2) % 5;

  HUB75_I2S_CFG::i2s_pins pins = { R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN };
  // profondeur de couleur la plus haute qui tient en memoire (256x64 demande le plus)
  for (int depth = 8; depth >= 4; depth--) {
    HUB75_I2S_CFG cfg(LAYOUT_PANEL_W[layout], 64, LAYOUT_CHAIN[layout], pins);
    cfg.setPixelColorDepthBits(depth);
    cfg.clkphase = false;
    dma = new MatrixPanel_I2S_DMA(cfg);
    if (dma->begin()) {
      Serial.printf("Dalle %s, couleurs %d bits\n", LAYOUT_NAME[layout], depth);
      break;
    }
    delete dma;
    dma = nullptr;
  }
  if (!dma) {  // impossible d'allouer la dalle : retour a 64x64 plutot que rester bloque
    Serial.println("Memoire insuffisante pour la dalle");
    if (layout != 0) {
      prefs.putUChar("layout", 0);
      delay(50);
      ESP.restart();
    }
    for (;;) delay(1000);
  }
  W = LAYOUT_PANEL_W[layout] * LAYOUT_CHAIN[layout];
  gfx = dma;
  if (LAYOUT_STACKED[layout]) { W = 128; H = 128; gfx = new StackedPanels(dma); }
  dma->setBrightness8(BRIGHT[brightIdx]);
  gfx->fillScreen(0);
  char m[24];
  snprintf(m, sizeof(m), "%s%s", LAYOUT_NAME[layout], demoOn ? " Demo" : "");
  showMessage(m);

  fftInit();
  adcInit();
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, NULL, 3, NULL, 0);
  autoSince = millis();
  printMenu();
}

void loop() {
  static uint32_t lastSeq = 0;
  handleButton();
  handleDemoPin();
  handleLayoutButton();
  handleSerial();
  uint8_t want = mode;
  if (mode == M_AUTO) {  // Auto : change de mode toutes les 20 s
    if (millis() - autoSince > AUTO_SECONDS * 1000UL) {
      autoSince = millis();
      autoMode = autoMode % (M_COUNT - 1) + 1;
      if (autoMode == M_GAME && !demoOn) autoMode = autoMode % (M_COUNT - 1) + 1;  // le jeu fait partie de la demo
    }
    want = autoMode;
  }
  if (want != shownMode) enterMode(want);
  if (shared.seq != lastSeq) {
    portENTER_CRITICAL(&audioMux);
    memcpy(&view, &shared, sizeof(view));
    portEXIT_CRITICAL(&audioMux);
    lastSeq = view.seq;
    float loud = -120;
    for (int c = 0; c < 2; c++)
      for (int b = 0; b < NBANDS; b++) loud = max(loud, view.bandDb[c][b]);
    agcUpdate(loud);
    updateMode(want);
    drawOverlay();
  }
  vTaskDelay(1);
}
