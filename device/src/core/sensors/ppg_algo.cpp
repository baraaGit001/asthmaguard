#include "ppg_algo.h"

#include <math.h>
#include <string.h>

namespace ag {

void PpgAlgo::add(uint32_t red, uint32_t ir) {
  red_[head_] = (float)red;
  ir_[head_] = (float)ir;
  head_ = (head_ + 1) % kWindow;
  if (n_ < kWindow) n_++;
}

namespace {
const int N = PpgAlgo::kWindow;
// Scratch (compute() is only called from one place, once per second).
float s_ir[N], s_red[N], s_bir[N], s_bred[N], s_f[N], s_tmp[N];

float mean(const float* x, int n) {
  double s = 0;
  for (int i = 0; i < n; i++) s += x[i];
  return (float)(s / n);
}

// x - moving average (centred, window w): removes DC and breathing drift.
void highpass(const float* x, float* y, int n, int w) {
  int h = w / 2;
  double s = 0;
  int lo = 0, hi = -1;
  for (int i = 0; i < n; i++) {
    int a = i - h < 0 ? 0 : i - h, b = i + h >= n ? n - 1 : i + h;
    while (hi < b) s += x[++hi];
    while (lo < a) s -= x[lo++];
    y[i] = x[i] - (float)(s / (hi - lo + 1));
  }
}

void smooth(const float* x, float* y, int n, int w) {
  int h = w / 2;
  for (int i = 0; i < n; i++) {
    int a = i - h < 0 ? 0 : i - h, b = i + h >= n ? n - 1 : i + h;
    float s = 0;
    for (int j = a; j <= b; j++) s += x[j];
    y[i] = s / (b - a + 1);
  }
}

// Robust peak-to-peak: 95th - 5th percentile (partial sort on a copy).
float p2p(const float* x, int n) {
  memcpy(s_tmp, x, sizeof(float) * n);
  // insertion sort is fine for 400 values once per second
  for (int i = 1; i < n; i++) {
    float v = s_tmp[i];
    int j = i - 1;
    while (j >= 0 && s_tmp[j] > v) { s_tmp[j + 1] = s_tmp[j]; j--; }
    s_tmp[j + 1] = v;
  }
  return s_tmp[(int)(n * 0.95f)] - s_tmp[(int)(n * 0.05f)];
}
}  // namespace

PpgResult PpgAlgo::compute() const {
  PpgResult r;
  if (!ready()) return r;
  for (int i = 0; i < N; i++) {
    int k = (head_ + i) % N;
    s_ir[i] = ir_[k];
    s_red[i] = red_[k];
  }
  float dc_ir = mean(s_ir, N), dc_red = mean(s_red, N);
  r.ir_dc = dc_ir;
  if (dc_ir < min_ir_dc || dc_red < min_ir_dc * 0.3f) { r.why = "no skin contact"; return r; }

  highpass(s_ir, s_bir, N, kRate);      // 1 s baseline
  highpass(s_red, s_bred, N, kRate);
  float ac_ir = p2p(s_bir, N), ac_red = p2p(s_bred, N);
  r.pi = ac_ir / dc_ir;
  if (r.pi < min_pi) { r.why = "signal too weak"; return r; }
  if (r.pi > max_pi) { r.why = "motion"; return r; }

  // Blood volume rises at systole -> reflected IR falls: find peaks of -IR.
  for (int i = 0; i < N; i++) s_tmp[i] = -s_bir[i];
  smooth(s_tmp, s_f, N, 7);
  float amp = p2p(s_f, N);
  float thr = 0.25f * amp;
  int peaks[40], np = 0;
  const int refractory = kRate * 30 / 100;  // 0.3 s -> max 200 bpm
  int last = -refractory;
  for (int i = 2; i < N - 2 && np < 40; i++) {
    float v = s_f[i];
    if (v > thr && v >= s_f[i - 1] && v >= s_f[i - 2] && v > s_f[i + 1] && v >= s_f[i + 2]) {
      if (i - last >= refractory) {
        peaks[np++] = i;
        last = i;
      } else if (v > s_f[peaks[np - 1]]) {
        peaks[np - 1] = i;  // keep the higher of two close peaks
        last = i;
      }
    }
  }
  r.beats = (uint8_t)np;
  if (np < 3) { r.why = "too few beats"; return r; }
  float ibi[39];
  int ni = np - 1;
  double s = 0, s2 = 0;
  for (int i = 0; i < ni; i++) {
    ibi[i] = (float)(peaks[i + 1] - peaks[i]);
    s += ibi[i];
    s2 += (double)ibi[i] * ibi[i];
  }
  double m = s / ni;
  double var = s2 / ni - m * m;
  r.ibi_cv = (float)(var > 0 ? sqrt(var) / m : 0);
  // median interval
  for (int i = 1; i < ni; i++) {
    float v = ibi[i];
    int j = i - 1;
    while (j >= 0 && ibi[j] > v) { ibi[j + 1] = ibi[j]; j--; }
    ibi[j + 1] = v;
  }
  float med = ni % 2 ? ibi[ni / 2] : 0.5f * (ibi[ni / 2 - 1] + ibi[ni / 2]);
  r.hr = 60.0f * kRate / med;

  float R = (ac_red / dc_red) / (ac_ir / dc_ir);
  r.r = R;
  float spo2 = R <= 0.337f ? 100.0f : -45.060f * R * R + 30.354f * R + 94.845f;
  if (spo2 > 100) spo2 = 100;
  r.spo2 = spo2;

  if (r.ibi_cv > max_ibi_cv) { r.why = "irregular beats"; return r; }
  if (r.hr < 35 || r.hr > 220) { r.why = "hr out of range"; return r; }
  if (R < 0.3f || R > 1.6f || spo2 < 70) { r.why = "spo2 out of range"; return r; }
  r.quality_ok = true;
  r.why = "ok";
  return r;
}

}  // namespace ag
