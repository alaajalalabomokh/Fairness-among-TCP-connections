#!/usr/bin/env bash
set -euo pipefail

# ========= config =========
RTTS="0 10 20 40 60 80 100 200 300 400 500"
NS3_SCRIPT="scratch/new.cc"
REPS=1
WARMUP_SEC=0
SIM_OUT_DIR="2FLOWS-FINAL-results"
# ==========================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

protos=( "ns3::TcpNewReno" "ns3::TcpCubic" "ns3::TcpDctcp" "ns3::TcpBbr" )

short() {
  local s="$1"; s="${s#ns3::Tcp}"; s="${s#ns3::}"
  case "$s" in
    CUBIC|Cubic) echo Cubic ;;
    BBR|Bbr)     echo BBR ;;
    DCTCP|Dctcp) echo DCTCP ;;
    NewReno)     echo NewReno ;;
    *)           echo "$s" ;;
  esac
}

mean_after_warmup() {
  local file="$1"
  LC_ALL=C awk -v w="$WARMUP_SEC" '$1+0>=w{ s+=($2+0); n++ } END{ printf("%.6f\n", n?s/n:0) }' "$file"
}

run_pair() {
  local tcp1="$1" tcp2="$2" root="$3"

  local s1 s2 base pair_dir results thr_png jain_png
  s1="$(short "$tcp1")"; s2="$(short "$tcp2")"
  base="--tcp1=$tcp1 --tcp2=$tcp2 --start1=0 --start2=0"

  pair_dir="$root/${s1}_${s2}"
  mkdir -p "$pair_dir"
  results="$pair_dir/results-${s1}_${s2}.dat"
  thr_png="$pair_dir/thr-${s1}_${s2}.png"
  jain_png="$pair_dir/thr-${s1}_${s2}-jain.png"
  : >"$results"

  echo "== Pair: $s1 vs $s2 =="

  local rtt sum1 sum2 rep avg1 avg2 f1 f2 jain
  for rtt in $RTTS; do
    echo "  RTT=$rtt ms"
    sum1=0; sum2=0

    for rep in $(seq 1 "$REPS"); do
      ./ns3 run "$NS3_SCRIPT $base --rttMs=$rtt"

      avg1=$(mean_after_warmup "$SIM_OUT_DIR/throughput1.dat")
      avg2=$(mean_after_warmup "$SIM_OUT_DIR/throughput2.dat")

      sum1=$(LC_ALL=C awk -v a="$sum1" -v b="$avg1" 'BEGIN{printf("%.10f", a+b)}')
      sum2=$(LC_ALL=C awk -v a="$sum2" -v b="$avg2" 'BEGIN{printf("%.10f", a+b)}')

      if [[ -d "$SIM_OUT_DIR" && "$SIM_OUT_DIR" == "2FLOWS-FINAL-results" ]]; then
        rm -f "$SIM_OUT_DIR/throughput1.dat" \
              "$SIM_OUT_DIR/throughput2.dat" \
              "$SIM_OUT_DIR/queueSize.dat" \
              "$SIM_OUT_DIR/jain.dat" 2>/dev/null || true
        rmdir "$SIM_OUT_DIR" 2>/dev/null || true
      fi
    done

    f1=$(LC_ALL=C awk -v s="$sum1" -v n="$REPS" 'BEGIN{printf("%.6f\n", n>0?s/n:0)}')
    f2=$(LC_ALL=C awk -v s="$sum2" -v n="$REPS" 'BEGIN{printf("%.6f\n", n>0?s/n:0)}')

    jain=$(LC_ALL=C awk -v x1="$f1" -v x2="$f2" 'BEGIN{
      den = 2.0*(x1*x1 + x2*x2);
      printf("%.6f\n", den>0 ? ((x1+x2)*(x1+x2))/den : 0.0);
    }')

    echo "$rtt $f1 $f2 $jain" >>"$results"
  done

  # plots
  gnuplot <<GP
set terminal pngcairo size 1000,700 enhanced font 'Verdana,10'
set output "$thr_png"
set title "RTT vs Throughput: $s1 vs $s2"
set xlabel "RTT (ms)"
set ylabel "Throughput (Mbps)"
set style data histograms
set style histogram cluster gap 1
set style fill solid border -1
set boxwidth 0.9
set grid ytics
set key top left
set datafile separator whitespace
plot "$results" using 2:xtic(1) title "$s1", "" using 3 title "$s2"
GP

  gnuplot <<GP
set terminal pngcairo size 900,600 enhanced font 'Verdana,10'
set output "$jain_png"
set title "Jain's Fairness vs RTT: $s1 vs $s2"
set xlabel "RTT (ms)"
set ylabel "Jain's Index"
set yrange [0:1]
set grid
set datafile separator whitespace
plot "$results" using 1:4 with linespoints lw 2 title "Jain"
GP
}

main() {
  local ts outroot
  ts="$(date +"%Y%m%d-%H%M%S")"
  outroot="$SCRIPT_DIR/results-$ts"
  mkdir -p "$outroot"

  local pi pj
  # unique unordered pairs
  for ((pi=0; pi<${#protos[@]}; pi++)); do
    for ((pj=pi; pj<${#protos[@]}; pj++)); do
      run_pair "${protos[pi]}" "${protos[pj]}" "$outroot"
    done
  done

  echo "✅ All outputs in: $outroot"
}

main "$@"
