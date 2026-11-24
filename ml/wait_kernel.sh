#!/bin/sh
# Wait for the Kaggle training kernel, then download its output.
#   ml/wait_kernel.sh <kaggle-user>/asthmaguard-cough-train [out_dir]
K=${KAGGLE:-kaggle}
REF=${1:?usage: wait_kernel.sh <kaggle-user>/asthmaguard-cough-train [out_dir]}
OUT=${2:-$(dirname "$0")/out/kaggle}
while :; do
  S=$($K kernels status "$REF" 2>&1 | tail -1)
  echo "$(date -u +%H:%M:%S) $S"
  case "$S" in
    *COMPLETE*|*ERROR*|*CANCEL*) break;;
  esac
  sleep 120
done
rm -rf "$OUT"; mkdir -p "$OUT"
$K kernels output "$REF" -p "$OUT" >/dev/null 2>&1
echo "KERNEL_FINISHED: $S"
