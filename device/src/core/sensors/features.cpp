#include "features.h"

#include <math.h>
#include <string.h>

namespace ag {
namespace feat {

static double hzToMel(double f) { return 2595.0 * log10(1.0 + f / 700.0); }
static double melToHz(double m) { return 700.0 * (pow(10.0, m / 2595.0) - 1.0); }

LogMel::LogMel() {
  const double pi = 3.14159265358979323846;
  for (int i = 0; i < kFrameLen; i++) window_[i] = (float)(0.5 - 0.5 * cos(2.0 * pi * i / kFrameLen));
  for (int i = 0; i < kFft / 2; i++) {
    cos_[i] = (float)cos(2.0 * pi * i / kFft);
    sin_[i] = (float)sin(2.0 * pi * i / kFft);
  }
  int bits = 0;
  while ((1 << bits) < kFft) bits++;
  for (int i = 0; i < kFft; i++) {
    int r = 0;
    for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
    rev_[i] = (uint16_t)r;
  }
  // Mel filterbank
  double mlo = hzToMel(kFMin), mhi = hzToMel(kFMax);
  double pts[kMel + 2];
  for (int i = 0; i < kMel + 2; i++) pts[i] = melToHz(mlo + (mhi - mlo) * i / (kMel + 1));
  int off = 0;
  for (int m = 0; m < kMel; m++) {
    double a = pts[m], b = pts[m + 1], c = pts[m + 2];
    int start = -1, len = 0;
    foff_[m] = (uint16_t)off;
    for (int k = 0; k < kBins; k++) {
      double f = (double)k * kSampleRate / kFft;
      double w = 0;
      if (f > a && f < c) w = f <= b ? (f - a) / (b - a) : (c - f) / (c - b);
      if (w > 0) {
        if (start < 0) start = k;
        if (len < 64 && off < kMel * 64) { fw_[off++] = (float)w; len++; }
      } else if (start >= 0) {
        break;
      }
    }
    if (start < 0) start = 0;  // degenerate filter (cannot happen with these settings)
    fstart_[m] = (uint16_t)start;
    flen_[m] = (uint16_t)len;
  }
}

void LogMel::frame(const int16_t* x, float* out) {
  for (int i = 0; i < kFft; i++) {
    float v = i < kFrameLen ? (x[i] / 32768.0f) * window_[i] : 0.0f;
    re_[rev_[i]] = v;
    im_[rev_[i]] = 0.0f;
  }
  for (int size = 2; size <= kFft; size <<= 1) {
    int half = size >> 1, step = kFft / size;
    for (int s = 0; s < kFft; s += size) {
      for (int j = 0; j < half; j++) {
        float wr = cos_[j * step], wi = -sin_[j * step];
        int p = s + j, q = p + half;
        float tr = re_[q] * wr - im_[q] * wi;
        float ti = re_[q] * wi + im_[q] * wr;
        re_[q] = re_[p] - tr;
        im_[q] = im_[p] - ti;
        re_[p] += tr;
        im_[p] += ti;
      }
    }
  }
  for (int m = 0; m < kMel; m++) {
    float e = 0;
    const float* w = &fw_[foff_[m]];
    for (int i = 0; i < flen_[m]; i++) {
      int k = fstart_[m] + i;
      e += w[i] * (re_[k] * re_[k] + im_[k] * im_[k]);
    }
    out[m] = logf(e + kLogFloor);
  }
}

void normalize(float* feat) {
  double s = 0;
  for (int i = 0; i < kFrames * kMel; i++) s += feat[i];
  float mean = (float)(s / (kFrames * kMel));
  for (int i = 0; i < kFrames * kMel; i++) feat[i] -= mean;
}

void computeWindow(LogMel& lm, const int16_t* pcm, float* out) {
  for (int f = 0; f < kFrames; f++) lm.frame(pcm + f * kHop, out + f * kMel);
  normalize(out);
}

float rmsDbfs(const int16_t* pcm, int n) {
  if (n <= 0) return -120.0f;
  double s = 0;
  for (int i = 0; i < n; i++) s += (double)pcm[i] * pcm[i];
  double rms = sqrt(s / n) / 32768.0;
  return rms > 1e-6 ? (float)(20.0 * log10(rms)) : -120.0f;
}

}  // namespace feat
}  // namespace ag

extern "C" {
int ag_feat_dims(int* frames, int* mel) {
  *frames = ag::feat::kFrames;
  *mel = ag::feat::kMel;
  return 0;
}
int ag_feat_window(const int16_t* pcm, int n, float* out) {
  if (n != ag::feat::kWindow) return -1;
  static ag::feat::LogMel lm;
  ag::feat::computeWindow(lm, pcm, out);
  return 0;
}
float ag_feat_rms_dbfs(const int16_t* pcm, int n) { return ag::feat::rmsDbfs(pcm, n); }
}
