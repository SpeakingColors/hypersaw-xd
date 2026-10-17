/*
 * HyperSaw : 3-layer JP-8000 style supersaw oscillator for KORG minilogue xd
 * Author: SpeakingColors
 *
 * Each layer = 7 detuned voices of a selectable waveform, using the
 * JP-8000 detune and mix curves (Adam Szabo, "How to Emulate the Super Saw", 2010).
 *
 * Controls
 *   SHAPE        : Detune (JP curve), also modulated by the xd's LFO / mod routing
 *   SHIFT+SHAPE  : Mix (center voice vs 6 side voices, JP curve)
 *   Wave A/B/C   : 0 Off, 1 Saw, 2 Square, 3 Pulse 25%, 4 Pulse 12%,
 *                  5 Triangle, 6 Sine, 7 Glass 1, 8 Glass 2, 9 Vox Ah
 *   Oct B / Oct C: -2..+2 octaves relative to layer A
 *   Layer Bal    : -100 = layer A only, 0 = equal, +100 = layers B/C louder
 */

#include "userosc.h"

#define NUM_LAYERS 3
#define NUM_VOICES 7

enum {
  W_OFF = 0, W_SAW, W_SQUARE, W_PULSE25, W_PULSE12,
  W_TRI, W_SINE, W_GLASS1, W_GLASS2, W_VOX, W_COUNT
};

// JP-8000 relative detune offsets (center voice is index 3)
static const float k_detune_offsets[NUM_VOICES] = {
  -0.11002313f, -0.06288439f, -0.01952356f, 0.f,
   0.01991221f,  0.06216538f,  0.10745242f
};

// Output gain and per-wave level trims (calibrated so all waves sit at a similar loudness)
static const float k_out_gain = 0.8f;
static const float k_wave_trim[W_COUNT] = {
  0.f,   // Off
  1.0f,  // Saw
  0.6f,  // Square
  0.6f,  // Pulse 25
  0.75f, // Pulse 12
  1.05f, // Triangle
  0.85f, // Sine
  0.7f,  // Glass 1
  0.95f, // Glass 2
  1.0f   // Vox Ah (final level set by k_vox_gain)
};

// "Ah" vowel formants: bandpass {b0 (incl. gain), a1, a2}; b1 = 0, b2 = -b0
#define NUM_FORMANTS 4
static const float k_formant[NUM_FORMANTS][3] = {
  { 0.00788823f, -1.97517136f, 0.98422353f }, // 730 Hz  (F1)
  { 0.00528542f, -1.96223771f, 0.98238195f }, // 1090 Hz (F2)
  { 0.00540993f, -1.86950050f, 0.96908609f }, // 2440 Hz (F3)
  { 0.00451083f, -1.77904561f, 0.95899244f }, // 3300 Hz (F4, "air")
};
static const float k_vox_gain    = 2.6f;   // overall Vox level
static const float k_vox_lp      = 0.1674f; // one-pole LPF ~1.4 kHz: softens the saw buzz
static const float k_vox_lp_comp = 2.2f;    // makes up level lost in the LPF
static const float k_vox_breath  = 0.6f;   // breath noise amount
static const float k_vox_drift   = 0.0035f; // max per-voice pitch drift (~6 cents)
static const float k_vox_detune  = 1.6f;    // Vox spreads wider: formants mask the beating

struct State {
  float phase[NUM_LAYERS][NUM_VOICES];
  uint8_t wave[NUM_LAYERS];
  int8_t  oct[NUM_LAYERS];
  float shape;       // detune knob 0..1
  float shiftshape;  // mix knob 0..1
  float balance;     // -1..1
  float hp_x1, hp_y1;
  float fz[NUM_LAYERS][NUM_FORMANTS][2]; // formant filter state per layer
  float vox_lp[NUM_LAYERS];               // source smoothing state
  float drift[NUM_LAYERS][NUM_VOICES];    // current pitch drift per voice
  float drift_t[NUM_LAYERS][NUM_VOICES];  // drift target
  uint32_t drift_tick;
};

static State s;

// ---------------------------------------------------------------------------
// JP-8000 curves (Szabo)
static inline float jp_detune_curve(float x) {
  // 11th order polynomial fit of the original detune knob response
  float y = 10028.7312891634f;
  y = y * x - 50818.8652045924f;
  y = y * x + 111363.4808729368f;
  y = y * x - 138150.6761080548f;
  y = y * x + 106649.6679158292f;
  y = y * x - 53046.9642751875f;
  y = y * x + 17019.9518580080f;
  y = y * x - 3425.0836591318f;
  y = y * x + 404.2703938388f;
  y = y * x - 24.1878824391f;
  y = y * x + 0.6717417634f;
  y = y * x + 0.0030115596f;
  return clipminmaxf(0.f, y, 1.f);
}

// ---------------------------------------------------------------------------
// Band-limited helpers (inv_dt precomputed to avoid divisions per sample)
static inline float poly_blep(float t, float dt, float inv_dt) {
  if (t < dt) {
    t *= inv_dt;
    return t + t - t * t - 1.f;
  } else if (t > 1.f - dt) {
    t = (t - 1.f) * inv_dt;
    return t * t + t + t + 1.f;
  }
  return 0.f;
}

static inline float saw_blep(float p, float dt, float inv_dt) {
  return (2.f * p - 1.f) - poly_blep(p, dt, inv_dt);
}

static inline float pulse_blep(float p, float dt, float inv_dt, float pw) {
  float q = p + pw;
  if (q >= 1.f) q -= 1.f;
  return saw_blep(p, dt, inv_dt) - saw_blep(q, dt, inv_dt);
}

// Renders one voice of one layer into acc[] (added). Wave switch is outside the sample loop.
static void render_voice(uint8_t w, float *phase, float dt, float g, float *acc, uint32_t frames) {
  float p = *phase;
  const float inv_dt = 1.f / dt;
  float pw = 0.5f;
  switch (w) {
  case W_SAW:
    for (uint32_t i = 0; i < frames; ++i) {
      acc[i] += g * saw_blep(p, dt, inv_dt);
      p += dt; if (p >= 1.f) p -= 1.f;
    }
    break;
  case W_PULSE25: pw = 0.25f;  goto pulse;
  case W_PULSE12: pw = 0.125f; goto pulse;
  case W_SQUARE:
  pulse:
    for (uint32_t i = 0; i < frames; ++i) {
      acc[i] += g * pulse_blep(p, dt, inv_dt, pw);
      p += dt; if (p >= 1.f) p -= 1.f;
    }
    break;
  case W_TRI:
    for (uint32_t i = 0; i < frames; ++i) {
      acc[i] += g * (2.f * si_fabsf(2.f * p - 1.f) - 1.f);
      p += dt; if (p >= 1.f) p -= 1.f;
    }
    break;
  case W_SINE:
    for (uint32_t i = 0; i < frames; ++i) {
      acc[i] += g * osc_sinf(p);
      p += dt; if (p >= 1.f) p -= 1.f;
    }
    break;
  case W_GLASS1: // sine phase-modulated by its 2nd harmonic: bright, glassy
    for (uint32_t i = 0; i < frames; ++i) {
      acc[i] += g * osc_sinf(p + 2.f + 0.12f * osc_sinf(2.f * p));
      p += dt; if (p >= 1.f) p -= 1.f;
    }
    break;
  case W_GLASS2: // 3rd harmonic PM + 5th harmonic shimmer, bell-like
    for (uint32_t i = 0; i < frames; ++i) {
      acc[i] += g * (0.8f * osc_sinf(p + 2.f + 0.18f * osc_sinf(3.f * p))
                   + 0.2f * osc_sinf(5.f * p));
      p += dt; if (p >= 1.f) p -= 1.f;
    }
    break;
  default: break;
  }
  *phase = p;
}

static void randomize_phases(void) {
  for (int l = 0; l < NUM_LAYERS; ++l)
    for (int v = 0; v < NUM_VOICES; ++v)
      s.phase[l][v] = (float)(osc_rand() >> 8) * 5.96046447753906e-008f; // /2^24
}

// ---------------------------------------------------------------------------
void OSC_INIT(uint32_t platform, uint32_t api) {
  (void)platform; (void)api;
  s.wave[0] = W_SAW; s.wave[1] = W_OFF; s.wave[2] = W_OFF;
  s.oct[0] = 0; s.oct[1] = 0; s.oct[2] = 0;
  s.shape = 0.5f;
  s.shiftshape = 0.5f;
  s.balance = 0.f;
  s.hp_x1 = s.hp_y1 = 0.f;
  for (int l = 0; l < NUM_LAYERS; ++l)
    for (int k = 0; k < NUM_FORMANTS; ++k)
      s.fz[l][k][0] = s.fz[l][k][1] = 0.f;
  for (int l = 0; l < NUM_LAYERS; ++l) {
    s.vox_lp[l] = 0.f;
    for (int v = 0; v < NUM_VOICES; ++v) s.drift[l][v] = s.drift_t[l][v] = 0.f;
  }
  s.drift_tick = 0;
  randomize_phases();
}

void OSC_CYCLE(const user_osc_param_t * const params, int32_t *yn, const uint32_t frames) {
  // Base pitch
  const float w0 = osc_w0f_for_note((params->pitch) >> 8, params->pitch & 0xFF);

  // Detune (knob + LFO), JP curve
  const float det_knob = clipminmaxf(0.f, s.shape + q31_to_f32(params->shape_lfo), 1.f);
  const float detune = jp_detune_curve(det_knob);

  // Mix, JP curve
  const float m = s.shiftshape;
  const float g_center = -0.55366f * m + 0.99785f;
  const float g_side   = -0.73764f * m * m + 1.2841f * m + 0.044372f;
  const float norm = 1.f / (g_center + 6.f * g_side);

  // Active layers (if everything is Off, layer A falls back to Saw)
  uint8_t wave[NUM_LAYERS] = { s.wave[0], s.wave[1], s.wave[2] };
  if (wave[0] == W_OFF && wave[1] == W_OFF && wave[2] == W_OFF) wave[0] = W_SAW;

  // Per-layer gains from balance
  const float bal = s.balance;
  const float gA  = (bal > 0.f) ? (1.f - bal) : 1.f;
  const float gBC = (bal < 0.f) ? (1.f + bal) : 1.f;
  float lgain[NUM_LAYERS] = { gA, gBC, gBC };

  float active_sum = 0.f;
  for (int l = 0; l < NUM_LAYERS; ++l)
    if (wave[l] != W_OFF) active_sum += lgain[l];
  const float layer_norm = (active_sum > 0.f) ? (1.f / active_sum) : 0.f;

  // Per-voice phase increments
  float dt[NUM_LAYERS][NUM_VOICES];
  float vgain[NUM_LAYERS][NUM_VOICES];
  float lowest_w = 0.5f;
  for (int l = 0; l < NUM_LAYERS; ++l) {
    const int8_t o = s.oct[l];
    const float wl = w0 * ((o >= 0) ? (float)(1 << o) : 1.f / (float)(1 << (-o)));
    if (wave[l] != W_OFF && wl < lowest_w) lowest_w = wl;
    for (int v = 0; v < NUM_VOICES; ++v) {
      const float det_l = (wave[l] == W_VOX) ? detune * k_vox_detune : detune;
      dt[l][v] = clipmaxf(wl * (1.f + det_l * k_detune_offsets[v]), 0.45f);
      const float g = (v == 3) ? g_center : g_side;
      vgain[l][v] = g * norm * lgain[l] * layer_norm * k_wave_trim[wave[l]];
    }
  }

  // JP-style high-pass at the fundamental of the lowest active layer (one-pole)
  const float hp_a = 1.f / (1.f + 6.2831853f * lowest_w);

  float acc[64];  // SDK guarantees frames <= 64
  for (uint32_t i = 0; i < frames; ++i) acc[i] = 0.f;
  for (int l = 0; l < NUM_LAYERS; ++l) {
    if (wave[l] == W_OFF) continue;
    if (wave[l] == W_VOX) {
      // Each voice slowly wanders around its pitch, like separate singers
      if ((s.drift_tick & 31) == 0) {
        for (int v = 0; v < NUM_VOICES; ++v)
          s.drift_t[l][v] = k_vox_drift * (2.f * (float)(osc_rand() >> 8) * 5.96046447753906e-008f - 1.f);
      }
      float lbuf[64];
      for (uint32_t i = 0; i < frames; ++i) lbuf[i] = 0.f;
      for (int v = 0; v < NUM_VOICES; ++v) {
        s.drift[l][v] += 0.02f * (s.drift_t[l][v] - s.drift[l][v]);
        const float dtv = clipmaxf(dt[l][v] * (1.f + s.drift[l][v]), 0.45f);
        render_voice(W_SAW, &s.phase[l][v], dtv, vgain[l][v], lbuf, frames);
      }
      // Soften the buzzy saw source, then add breath noise before the vowel filters
      float lp = s.vox_lp[l];
      const float breath = k_vox_breath * lgain[l] * layer_norm;
      for (uint32_t i = 0; i < frames; ++i) {
        lp += k_vox_lp * (lbuf[i] - lp);
        lbuf[i] = lp * k_vox_lp_comp + breath * osc_white();
      }
      s.vox_lp[l] = lp;
      for (int k = 0; k < NUM_FORMANTS; ++k) {
        const float b0 = k_formant[k][0] * k_vox_gain, a1 = k_formant[k][1], a2 = k_formant[k][2];
        float z1 = s.fz[l][k][0], z2 = s.fz[l][k][1];
        for (uint32_t i = 0; i < frames; ++i) {   // transposed direct form II
          const float x = lbuf[i];
          const float yv = b0 * x + z1;
          z1 = -a1 * yv + z2;
          z2 = -b0 * x - a2 * yv;
          acc[i] += yv;
        }
        s.fz[l][k][0] = z1; s.fz[l][k][1] = z2;
      }
    } else {
      for (int v = 0; v < NUM_VOICES; ++v)
        render_voice(wave[l], &s.phase[l][v], dt[l][v], vgain[l][v], acc, frames);
    }
  }

  ++s.drift_tick;

  float hp_x1 = s.hp_x1, hp_y1 = s.hp_y1;
  q31_t * __restrict y = (q31_t *)yn;
  for (uint32_t i = 0; i < frames; ++i) {
    const float sig = acc[i];
    const float hp = hp_a * (hp_y1 + sig - hp_x1);   // one-pole HPF
    hp_x1 = sig;
    hp_y1 = hp;
    y[i] = f32_to_q31(osc_softclipf(0.05f, hp * k_out_gain));
  }

  s.hp_x1 = hp_x1;
  s.hp_y1 = hp_y1;
}

void OSC_NOTEON(const user_osc_param_t * const params) {
  (void)params;
  randomize_phases();   // JP-8000 free-running / random phase behaviour
}

void OSC_NOTEOFF(const user_osc_param_t * const params) {
  (void)params;
}

static inline int8_t clamp_oct(int v) {
  return (int8_t)((v < -2) ? -2 : (v > 2) ? 2 : v);
}

void OSC_PARAM(uint16_t index, uint16_t value) {
  switch (index) {
  case k_user_osc_param_id1: s.wave[0] = (value < W_COUNT) ? value : W_OFF; break;
  case k_user_osc_param_id2: s.wave[1] = (value < W_COUNT) ? value : W_OFF; break;
  case k_user_osc_param_id3: s.wave[2] = (value < W_COUNT) ? value : W_OFF; break;
  // bipolar % params arrive as 0..200 with 0 at 100
  case k_user_osc_param_id4: s.oct[1] = clamp_oct((int)value - 100); break;
  case k_user_osc_param_id5: s.oct[2] = clamp_oct((int)value - 100); break;
  case k_user_osc_param_id6:
    s.balance = clipminmaxf(-1.f, ((int)value - 100) * 0.01f, 1.f);
    break;
  case k_user_osc_param_shape:      s.shape = param_val_to_f32(value); break;
  case k_user_osc_param_shiftshape: s.shiftshape = param_val_to_f32(value); break;
  default: break;
  }
}
