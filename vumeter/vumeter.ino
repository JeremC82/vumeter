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
/*    appui court        : mode suivant (Auto, Barres, Spectre, Miroir, Aiguilles, Stereo, Onde)  */
/*    appui 1 a 5 s      : luminosite suivante (5 niveaux)                                       */
/*    appui 5 s          : configuration de dalle suivante (redemarre)                            */
/*                                                                                                */
/*  Mode demo : cavalier permanent entre GPIO 18 et la masse. Le son est alors une petite        */
/*  musique synthetisee (batterie, basse, arpege stereo) a la place de l'entree audio.           */
/*  Le cavalier peut etre mis ou retire a tout moment.                                           */
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
// entrees audio : ADC1 canal 0 = GPIO 36 (gauche), canal 3 = GPIO 39 (droite)
#define ADC_CH_L ADC1_CHANNEL_0
#define ADC_CH_R ADC1_CHANNEL_3

// ---------------------------------------------------------------- reglages
const char *LAYOUT_NAME[4] = { "64x64", "2x64x64", "128x64", "2x128x64" };
const uint8_t LAYOUT_PANEL_W[4] = { 64, 64, 128, 128 };
const uint8_t LAYOUT_CHAIN[4] = { 1, 2, 1, 2 };
enum Mode : uint8_t { M_AUTO, M_BARS, M_SPECTRUM, M_MIRROR, M_NEEDLES, M_STEREO, M_WAVE, M_COUNT };
const char *MODE_NAME[M_COUNT] = { "Auto", "Barres", "Spectre", "Miroir", "Aiguilles", "Stereo", "Onde" };
const uint8_t BRIGHT[5] = { 20, 45, 80, 140, 220 };
#define AUTO_SECONDS 20

Preferences prefs;
MatrixPanel_I2S_DMA *dma = nullptr;
int W = 64;               // largeur de l'ecran (64, 128 ou 256)
const int H = 64;
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
uint16_t dimmed(uint16_t c, int shift) {  // meme teinte, plus sombre (cases eteintes)
  uint8_t r = ((c >> 11) & 31) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
  return rgb(r >> shift, g >> shift, b >> shift);
}
uint16_t zoneColor(float f) {  // vert, jaune, rouge selon la hauteur
  if (f > 0.88f) return rgb(255, 30, 20);
  if (f > 0.72f) return rgb(255, 190, 0);
  return rgb(20, 220, 40);
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
#define MAX_BARS 128
Bar barList[MAX_BARS];
int nBars = 0;
const int SEG = 2;  // hauteur d'une case : 1 ligne allumee + 1 ligne d'espace
int segCount(const Bar &br) { return br.center ? H / 4 : H / SEG; }
uint16_t barCellColor(const Bar &br, int seg, bool lit, bool cap) {
  int n = segCount(br);
  float f = (seg + 0.5f) / n;
  uint16_t c = br.center ? hsv(br.hue, 1, 0.35f + 0.65f * f) : hsv(120 - 120 * f, 1, 1);
  if (cap) return br.center ? rgb(255, 255, 255) : rgb(255, 255, 255);
  return lit ? c : dimmed(c, 4);
}
void barDrawSeg(const Bar &br, int seg, bool lit, bool cap) {
  uint16_t col = barCellColor(br, seg, lit, cap);
  if (br.center) {  // depuis le milieu vers le haut et vers le bas
    int yu = H / 2 - 2 - seg * SEG, yd = H / 2 + 1 + seg * SEG;
    dma->fillRect(br.x, yu, br.w, 1, col);
    dma->fillRect(br.x, yd, br.w, 1, col);
  } else {
    dma->fillRect(br.x, H - 1 - seg * SEG, br.w, 1, col);
  }
}
void barsFullDraw() {
  dma->fillScreen(0);
  for (int i = 0; i < nBars; i++) {
    Bar &br = barList[i];
    br.shown = 0;
    br.cap = -1;
    for (int s = 0; s < segCount(br); s++) barDrawSeg(br, s, false, false);
  }
}
void barsSetup(bool stereoSplit, bool center) {
  int barW = W >= 256 ? 3 : W >= 128 ? 3 : 3, gap = 1, pitch = barW + gap;
  nBars = 0;
  if (!stereoSplit) {
    int n = min(MAX_BARS, W / pitch), x0 = (W - n * pitch + gap) / 2;
    for (int i = 0; i < n; i++)
      barList[nBars++] = { (int16_t)(x0 + i * pitch), (int16_t)barW, 2, (uint8_t)(i * NBANDS / n), (uint8_t)((i + 1) * NBANDS / n), center,
                        (float)(i * 300 / n), Ballistic(), 0, -1 };
  } else {  // gauche : graves au centre vers la gauche ; droite : graves au centre vers la droite
    int half = W / 2, n = min(MAX_BARS / 2, half / pitch);
    for (int i = 0; i < n; i++) {
      uint8_t b0 = i * NBANDS / n, b1 = (i + 1) * NBANDS / n;
      barList[nBars++] = { (int16_t)(half - (i + 1) * pitch + gap), (int16_t)barW, 0, b0, b1, center, 200.0f - 200.0f * i / n, Ballistic(), 0, -1 };
      barList[nBars++] = { (int16_t)(half + i * pitch), (int16_t)barW, 1, b0, b1, center, 200.0f - 200.0f * i / n, Ballistic(), 0, -1 };
    }
  }
  barsFullDraw();
}
void barsUpdate() {
  for (int i = 0; i < nBars; i++) {
    Bar &br = barList[i];
    float db = -120;
    for (int b = br.band0; b < max(br.band0 + 1, (int)br.band1); b++)
      for (int c = 0; c < 2; c++)
        if (br.ch == 2 || br.ch == c) db = max(db, view.bandDb[c][b]);
    br.b.update(norm01(db));
    int n = segCount(br);
    int lit = (int)(br.b.v * n + 0.5f), cap = min(n - 1, (int)(br.b.peak * n));
    if (cap < lit || br.b.peak < 0.02f) cap = -1;
    for (int s = min(lit, (int)br.shown); s < max(lit, (int)br.shown); s++) barDrawSeg(br, s, s < lit, false);
    if (br.cap != cap) {
      if (br.cap >= 0) barDrawSeg(br, br.cap, br.cap < lit, false);
      if (cap >= 0) barDrawSeg(br, cap, false, true);
    }
    br.shown = lit;
    br.cap = cap;
  }
}

// ---------------------------------------------------------------- mode Barres : VU stereo horizontal a segments
Ballistic vuBar[2];
int vuShown[2] = { 0, 0 }, vuCap[2] = { -1, -1 };
const int VU_LBL = 8;
int vuSegW() { return W >= 256 ? 5 : W >= 128 ? 4 : 3; }
int vuSegs() { return (W - VU_LBL) / vuSegW(); }
void vuDrawSeg(int c, int s, bool lit, bool cap) {
  int n = vuSegs(), sw = vuSegW(), x = VU_LBL + s * sw, y = c ? 36 : 6;
  float f = (s + 1.0f) / n;
  uint16_t col = cap ? rgb(255, 255, 255) : lit ? zoneColor(f) : dimmed(zoneColor(f), 4);
  dma->fillRect(x, y, sw - 1, 22, col);
}
void vuBarsSetup() {
  dma->fillScreen(0);
  dma->setFont(NULL);
  dma->setTextSize(1);
  dma->setTextColor(rgb(160, 160, 160));
  dma->setCursor(1, 13);
  dma->print("L");
  dma->setCursor(1, 43);
  dma->print("R");
  // echelle entre les deux barres : graduations a -40 -30 -20 -10 -6 -3 0 dB (sous la reference)
  static const int8_t TICKS[7] = { -40, -30, -20, -10, -6, -3, 0 };
  int n = vuSegs(), sw = vuSegW();
  dma->drawFastHLine(VU_LBL, 31, n * sw - 1, rgb(60, 60, 60));
  for (int t = 0; t < 7; t++) {
    float f = (TICKS[t] + RANGE_DB) / RANGE_DB;
    int x = VU_LBL + (int)(f * n * sw) - 1;
    dma->drawFastVLine(constrain(x, VU_LBL, W - 2), 29, 5, TICKS[t] >= -3 ? rgb(200, 40, 30) : rgb(120, 120, 120));
  }
  for (int c = 0; c < 2; c++) {
    vuShown[c] = 0;
    vuCap[c] = -1;
    for (int s = 0; s < n; s++) vuDrawSeg(c, s, false, false);
  }
}
void vuBarsUpdate() {
  int n = vuSegs();
  for (int c = 0; c < 2; c++) {
    vuBar[c].update(view.rmsDb[c] < GATE_DB ? 0 : norm01(view.rmsDb[c] + 6));
    int lit = (int)(vuBar[c].v * n + 0.5f), cap = min(n - 1, (int)(vuBar[c].peak * n));
    if (cap < lit || vuBar[c].peak < 0.02f) cap = -1;
    for (int s = min(lit, vuShown[c]); s < max(lit, vuShown[c]); s++) vuDrawSeg(c, s, s < lit, false);
    if (vuCap[c] != cap) {
      if (vuCap[c] >= 0) vuDrawSeg(c, vuCap[c], vuCap[c] < lit, false);
      if (cap >= 0) vuDrawSeg(c, cap, false, true);
    }
    vuShown[c] = lit;
    vuCap[c] = cap;
  }
}

// ---------------------------------------------------------------- mode Aiguilles : deux vu-metres analogiques
GFXcanvas16 *dialCv = nullptr;
int dialW, dialH;
float needle[2] = { 0, 0 }, needleV[2] = { 0, 0 };
int clipHold[2] = { 0, 0 };
float vuPos(float dbVU) {  // loi des vu-metres : -20 VU a gauche, +3 VU a droite
  float lin = powf(10, dbVU / 20), lo = powf(10, -20 / 20.0f), hi = powf(10, 3 / 20.0f);
  return constrain((lin - lo) / (hi - lo), 0.0f, 1.0f);
}
void needlesSetup() {
  dma->fillScreen(0);
  if (W >= 128) { dialW = W / 2; dialH = H; } else { dialW = W; dialH = H / 2; }
  if (dialCv) delete dialCv;
  dialCv = new GFXcanvas16(dialW, dialH);
}
void drawDial(int c) {
  GFXcanvas16 &cv = *dialCv;
  uint16_t face = rgb(120, 82, 20), ink = rgb(10, 6, 0), red = rgb(200, 20, 10);
  cv.fillScreen(face);
  cv.drawRect(0, 0, dialW, dialH, rgb(40, 26, 6));
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
  if (clipHold[c] > 0) cv.fillCircle(dialW - 5, dialH - 6, 2, rgb(255, 30, 20));  // voyant de crete
  float a = A0 + (A1 - A0) * needle[c];
  cv.drawLine(px, py, px + sinf(a) * (r - 1), py - cosf(a) * (r - 1), ink);
  cv.drawLine(px + 1, py, px + 1 + sinf(a) * (r - 1), py - cosf(a) * (r - 1), ink);
  int ox = W >= 128 ? c * dialW : 0, oy = W >= 128 ? 0 : c * dialH;
  dma->drawRGBBitmap(ox, oy, cv.getBuffer(), dialW, dialH);
}
void needlesUpdate() {
  if (!dialCv || !dialCv->getBuffer()) return;
  for (int c = 0; c < 2; c++) {
    // 0 VU a 8 dB sous le haut de l'echelle automatique
    float target = view.rmsDb[c] < GATE_DB ? 0 : vuPos(view.rmsDb[c] - (agcRef - 8));
    needleV[c] += (target - needle[c]) * 0.09f;  // aiguille avec inertie
    needleV[c] *= 0.72f;
    needle[c] = constrain(needle[c] + needleV[c], -0.02f, 1.02f);
    if (view.peakDb[c] > agcRef + 2) clipHold[c] = 15;
    else if (clipHold[c] > 0) clipHold[c]--;
    drawDial(c);
  }
}

// ---------------------------------------------------------------- mode Onde : oscilloscope gauche (haut) et droite (bas)
int8_t waveLo[2][256], waveHi[2][256];
void waveSetup() {
  dma->fillScreen(0);
  for (int c = 0; c < 2; c++)
    for (int x = 0; x < 256; x++) waveLo[c][x] = waveHi[c][x] = -1;
}
void waveUpdate() {
  float gain = powf(10, -(agcRef - 6) / 20) / 2048.0f * 15;  // la musique remplit la moitie d'ecran
  int per = max(1, WAVE_N / W);
  for (int c = 0; c < 2; c++) {
    int y0 = c ? 47 : 15;
    uint16_t col = c ? rgb(255, 120, 20) : rgb(30, 200, 255);
    for (int x = 0; x < W; x++) {
      int lo = 99, hi = -99;
      for (int k = 0; k < per; k++) {
        int y = (int)(view.wave[c][x * per + k] * gain);
        lo = min(lo, y);
        hi = max(hi, y);
      }
      lo = constrain(y0 - hi, c * 32, c * 32 + 31);
      hi = constrain(y0 - min(lo, hi) , c * 32, c * 32 + 31);
      int oldLo = waveLo[c][x], oldHi = waveHi[c][x];
      if (oldLo == lo && oldHi == hi) continue;
      if (oldLo >= 0) dma->drawFastVLine(x, oldLo, oldHi - oldLo + 1, 0);
      dma->drawPixel(x, y0, rgb(30, 30, 30));
      dma->drawFastVLine(x, lo, hi - lo + 1, col);
      waveLo[c][x] = lo;
      waveHi[c][x] = hi;
    }
  }
}

// ---------------------------------------------------------------- choix du mode
uint8_t shownMode = 255;
uint32_t autoSince = 0;
uint8_t autoMode = M_BARS;
void enterMode(uint8_t m) {
  if (dialCv && m != M_NEEDLES) { delete dialCv; dialCv = nullptr; }
  switch (m) {
    case M_BARS: vuBarsSetup(); break;
    case M_SPECTRUM: barsSetup(false, false); break;
    case M_MIRROR: barsSetup(false, true); break;
    case M_NEEDLES: needlesSetup(); break;
    case M_STEREO: barsSetup(true, false); break;
    case M_WAVE: waveSetup(); break;
  }
  shownMode = m;
}
void updateMode(uint8_t m) {
  switch (m) {
    case M_BARS: vuBarsUpdate(); break;
    case M_SPECTRUM: case M_MIRROR: case M_STEREO: barsUpdate(); break;
    case M_NEEDLES: needlesUpdate(); break;
    case M_WAVE: waveUpdate(); break;
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
    dma->fillRect(0, 0, w, 10, 0);
    dma->drawRect(0, 0, w, 10, rgb(80, 80, 80));
    dma->setFont(NULL);
    dma->setTextSize(1);
    dma->setTextColor(rgb(255, 255, 255));
    dma->setCursor(2, 1);
    dma->print(overlay);
    shown = true;
  } else if (shown) {  // message parti : ecran redessine
    shown = false;
    enterMode(shownMode);
  }
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
    layout = (layout + 1) % 4;
    prefs.putUChar("layout", layout);
    char m[24];
    snprintf(m, sizeof(m), "Dalle %s", LAYOUT_NAME[layout]);
    showMessage(m);
    drawOverlay();
    delay(1500);
    ESP.restart();
  }
  if (!pressed && down) {
    down = false;
    uint32_t d = now - downAt;
    if (d < 40) return;  // rebond
    if (d < 1000) {      // court : mode suivant
      mode = (mode + 1) % M_COUNT;
      prefs.putUChar("mode", mode);
      showMessage(MODE_NAME[mode]);
      autoSince = now;
    } else if (d < 5000) {  // 1 a 3 s : luminosite
      brightIdx = (brightIdx + 1) % 5;
      dma->setBrightness8(BRIGHT[brightIdx]);
      prefs.putUChar("bright", brightIdx);
      char m[24];
      snprintf(m, sizeof(m), "Lum %d/5", brightIdx + 1);
      showMessage(m);
    }
  }
}

// ---------------------------------------------------------------- cavalier du mode demo (anti-rebond 100 ms)
void handleDemoPin() {
  static uint32_t last = 0;
  static uint8_t same = 0;
  if (millis() - last < 20) return;
  last = millis();
  bool want = digitalRead(DEMO_PIN) == LOW;
  if (want == demoOn) { same = 0; return; }
  if (++same < 5) return;
  same = 0;
  demoOn = want;
  showMessage(want ? "Demo" : "Entree audio");
}

// ---------------------------------------------------------------- demarrage
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);  // pas de wifi : toute la memoire pour la dalle
  btStop();
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(DEMO_PIN, INPUT_PULLUP);
  delay(5);
  demoOn = digitalRead(DEMO_PIN) == LOW;
  prefs.begin("vu", false);
  layout = prefs.getUChar("layout", 0) % 4;
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
  if (!dma) {  // impossible d'allouer la dalle
    Serial.println("Memoire insuffisante pour la dalle");
    for (;;) delay(1000);
  }
  W = LAYOUT_PANEL_W[layout] * LAYOUT_CHAIN[layout];
  dma->setBrightness8(BRIGHT[brightIdx]);
  dma->fillScreen(0);
  char m[24];
  snprintf(m, sizeof(m), "%s%s", LAYOUT_NAME[layout], demoOn ? " Demo" : "");
  showMessage(m);

  fftInit();
  adcInit();
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, NULL, 3, NULL, 0);
  autoSince = millis();
}

void loop() {
  static uint32_t lastSeq = 0;
  handleButton();
  handleDemoPin();
  uint8_t want = mode;
  if (mode == M_AUTO) {  // Auto : change de mode toutes les 20 s
    if (millis() - autoSince > AUTO_SECONDS * 1000UL) {
      autoSince = millis();
      autoMode = autoMode % (M_COUNT - 1) + 1;
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
