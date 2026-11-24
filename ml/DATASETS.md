# Datasets

All data is read inside the Kaggle kernel (`ml/make_kernel.py` → `<kaggle-user>/asthmaguard-cough-train`); nothing is redistributed in this repo. The licenses below are the ones published by the **original** dataset authors; the Kaggle mirrors are re-uploads.

| Use | Kaggle mirror | Original source | License | How it is used |
|---|---|---|---|---|
| cough (positive) | `orvile/coughvid-v3` | COUGHVID, Orlandic et al., *Scientific Data* 8, 156 (2021), Zenodo 7024894 | CC BY 4.0 | recordings with `cough_detected` ≥ 0.9; up to 6 cough onsets per recording |
| cough (positive) | `sarabhian/coswara-dataset-heavy-cough` | Coswara, Sharma et al., Interspeech 2020, github.com/iiscleap/Coswara-Data | CC BY 4.0 | `cough*.wav` per participant; split by participant id |
| noise + cough | `eliasmarcon/environmental-sound-classification-50` | ESC-50, K. J. Piczak, ACM MM 2015, github.com/karolpiczak/ESC-50 | CC BY-NC 3.0 | 49 non-cough classes → noise (sneezing, laughing, breathing, snoring, clapping, keyboard, door, street …); class `coughing` → cough |
| talking + background noise | `yashdogra/speech-commands` | Speech Commands v0.02, P. Warden, arXiv:1804.03209 | CC BY 4.0 | spoken words (split by speaker) + the `_background_noise_` files |

Notes
- ESC-50 is **non-commercial** (CC BY-NC). Fine for this student prototype; a product would need to replace it.
- COUGHVID's `cough_detected` is its authors' own classifier score; it is used only to skip recordings with no cough at all.
- Split is by recording / person / ESC source clip (hash of the group id → 70/15/15), never by window, so the same person never appears in two splits.
- Wheeze is not modelled (future work: chest sensor). Throat clearing has no clean public source here; the team's own recordings (section 7.6) should include it.
- Section 7.6 (on-wrist INMP441 recordings) is done by the team; add them under `ml/data/wrist/{cough,noise}/*.wav` and re-run with `AG_INPUT`.
