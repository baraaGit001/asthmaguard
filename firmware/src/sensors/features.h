// Log-mel features for the cough model (section 7.3).
//
// THE SAME FILE is compiled into ml/ (as a shared library, through ctypes)
// to build the training features, so the board and the training pipeline
// can never drift apart. ml/tests/test_features.py checks it against an
// independent numpy implementation.
//
//   16 kHz mono int16, 1 s window
//   frame 512 samples (32 ms), hop 320 (20 ms) -> 49 frames
//   periodic Hann window, 512-point FFT, power spectrum
//   40 triangular mel filters (HTK mel scale), 20 Hz .. 8000 Hz
//   ln(energy + 1e-6), then the window mean is subtracted (level invariance)
#pragma once
#include <stdint.h>

namespace ag {
namespace feat {

constexpr int kSampleRate = 16000;
constexpr int kWindow = 16000;
constexpr int kFrameLen = 512;
constexpr int kFft = 512;
constexpr int kHop = 320;
constexpr int kFrames = 1 + (kWindow - kFrameLen) / kHop;  // 49
constexpr int kMel = 40;
constexpr int kBins = kFft / 2 + 1;                          // 257
constexpr float kFMin = 20.0f;
constexpr float kFMax = 8000.0f;
constexpr float kLogFloor = 1e-6f;

class LogMel {
 public:
  LogMel();
  // One frame: kFrameLen int16 samples -> kMel log energies (not normalised).
  void frame(const int16_t* x, float* out);

 private:
  float window_[kFrameLen];
  float cos_[kFft / 2], sin_[kFft / 2];
  uint16_t rev_[kFft];
  // Sparse mel filterbank: for each filter, bins [start, start+len).
  uint16_t fstart_[kMel], flen_[kMel];
  float fw_[kMel * 64];   // weights, packed
  uint16_t foff_[kMel];   // offset into fw_
  float re_[kFft], im_[kFft];
};

// Subtract the mean of all kFrames*kMel values (in place).
void normalize(float* feat);

// Full window: kWindow samples -> kFrames*kMel normalised features.
void computeWindow(LogMel& lm, const int16_t* pcm, float* out);

// RMS of int16 PCM, in dBFS. Used to skip silent windows.
float rmsDbfs(const int16_t* pcm, int n);

}  // namespace feat
}  // namespace ag

// C entry points for ctypes (ml/features_lib.py).
extern "C" {
int ag_feat_dims(int* frames, int* mel);
// pcm: n int16 samples (n must be kWindow). out: kFrames*kMel floats.
int ag_feat_window(const int16_t* pcm, int n, float* out);
float ag_feat_rms_dbfs(const int16_t* pcm, int n);
}
