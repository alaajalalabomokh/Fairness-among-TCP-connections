#!/usr/bin/env bash
# Usage:
# ./alaaautotest.sh <ns3_script> "<base_args or EMPTY>" <results-base.dat> <plot-base.png> "<title>"
# Example:
# ./alaaautotest.sh scratch/Alaafinal.cc "" results.dat thr.png "RTT asymmetry (Flow1=100ms)"

set -euo pipefail

N=1
WARMUP_SEC=0
FACTORS="0.1 0.2 0.4 0.6 0.8 1.0 1.2 1.4 1.6 1.8 2.0"

RTT1=100   # ms
BNECK=5    # ms  (min RTT any flow can have is 2*BNECK = 10ms)

RAW_DIR="2FLOWS-FINAL-results"  # where the C++ writes its outputs

map_proto() {
  case "$1" in
    BBR)     echo "ns3::TcpBbr" ;;
    Cubic)   echo "ns3::TcpCubic" ;;
    NewReno) echo "ns3::TcpNewReno" ;;
    DCTCP)   echo "ns3::TcpDctcp" ;;
    *)       echo "$1" ;;
  esac
}

# mean of column-2 after time >= WARMUP_SEC
mean_after_warmup() {
  local file="$1"
  LC_ALL=C awk -v w="$WARMUP_SEC" '
    $1+0>=w { s+=($2+0); n++ }
    END { printf("%.6f\n", n ? s/n : 0) }' "$file"
}

NS3_SCRIPT=${1:?}
BASE_ARGS=${2:-""}
RESULTS_BASE=${3:?}   # e.g. results.dat
PNG_BASE=${4:?}       # e.g. thr.png
TITLE=${5:?}

# All 16 ordered pairs
PROTOS=(BBR Cubic NewReno DCTCP)
PAIRS=()
for A in "${PROTOS[@]}"; do
  for B in "${PROTOS[@]}"; do
    PAIRS+=("${A}/${B}")
  done
done

TIMESTAMP=$(date +"%Y%m%d-%H%M%S")
OUTDIR="scratch/results-${TIMESTAMP}"
mkdir -p "$OUTDIR"

for pair in "${PAIRS[@]}"; do
  A=${pair%/*}
  B=${pair#*/}

  TCP1_TYPE=$(map_proto "$A")
  TCP2_TYPE=$(map_proto "$B")

  TAG="${A}_${B}"
  PAIR_DIR="$OUTDIR/$TAG"
  mkdir -p "$PAIR_DIR"

  RESULTS_FILE="$PAIR_DIR/${RESULTS_BASE%.*}.dat"
  THR_PNG="$PAIR_DIR/${PNG_BASE%.*}.png"
  JAIN_PNG="${PAIR_DIR}/${PNG_BASE%.*}-jain.png"
  : >"$RESULTS_FILE"

  echo "=== Pair $A/$B ==="
  echo "Sweeping --rtt2Factor over: $FACTORS"

  for k in $FACTORS; do
    echo "  → --rtt2Factor=$k"

    TMP_THR1="$(mktemp)"
    TMP_THR2="$(mktemp)"

    for i in $(seq 1 $N); do
      ./ns3 run "$NS3_SCRIPT" -- \
        --tcp1="$TCP1_TYPE" --tcp2="$TCP2_TYPE" \
        --rtt1Ms="$RTT1" --rtt2Factor="$k" --bottleneckDelayMs="$BNECK" \
        $BASE_ARGS --RngRun="$i"

      avg_thr1=$(mean_after_warmup "$RAW_DIR/throughput1.dat")
      avg_thr2=$(mean_after_warmup "$RAW_DIR/throughput2.dat")

      echo "$avg_thr1" >>"$TMP_THR1"
      echo "$avg_thr2" >>"$TMP_THR2"
    done

    final_thr1=$(awk '{s+=$1} END{printf("%.6f\n", NR?s/NR:0)}' "$TMP_THR1")
    final_thr2=$(awk '{s+=$1} END{printf("%.6f\n", NR?s/NR:0)}' "$TMP_THR2")

    rm -f "$TMP_THR1" "$TMP_THR2"

    final_jain=$(
      LC_ALL=C awk -v x1="$final_thr1" -v x2="$final_thr2" '
        BEGIN{
          den = 2.0 * (x1*x1 + x2*x2);
          if (den > 0) printf("%.6f\n", ((x1+x2)*(x1+x2))/den);
          else          printf("0.000000\n");
        }'
    )

    echo "$k $final_thr1 $final_thr2 $final_jain" >>"$RESULTS_FILE"
  done

  # per-flow tables from results.dat
  awk '{print $1, $2}' "$RESULTS_FILE" > "$PAIR_DIR/flow1.dat"   # k vs thr1
  awk '{print $1, $3}' "$RESULTS_FILE" > "$PAIR_DIR/flow2.dat"   # k vs thr2

  # ---- plots for this pair ----
  gnuplot <<GP
set terminal pngcairo size 1000,700 enhanced font 'Verdana,10'
set output "$THR_PNG"
set title "$TITLE ($A/$B)"
set xlabel "--rtt2Factor (k)"
set ylabel "Throughput (Mbps)"
set style data histograms
set style histogram cluster gap 1
set style fill solid border -1
set boxwidth 0.9
set grid ytics
set key top left
set datafile separator whitespace
plot "$RESULTS_FILE" using 2:xtic(1) title "$A", \
     "" using 3 title "$B"
GP

  gnuplot <<GP
set terminal pngcairo size 900,600 enhanced font 'Verdana,10'
set output "$JAIN_PNG"
set title "Jain's Fairness vs rtt2Factor ($A/$B)"
set xlabel "--rtt2Factor (k)"
set ylabel "Jain's Index"
set yrange [0.0:1.0]
set grid
set datafile separator whitespace
plot "$RESULTS_FILE" using 1:4 with linespoints lw 2 title "Jain"
GP

  echo "✅ Saved in: $PAIR_DIR"
done

echo "All outputs in: $OUTDIR"
