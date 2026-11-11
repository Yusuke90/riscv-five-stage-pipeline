#!/bin/bash
# Usage: bash run_tests.sh [path-to-rvsim]
# For every program in tests/, under every predictor and both hazard modes:
#   1. the register file must equal the single-cycle oracle's
#   2. retired must equal the single-cycle cycle count
#   3. cycles = retired + 4 + 2*redirects + stalls
#   4. redirects = mispredicts + jalr
# Then, for the four course programs, it checks the golden cycle and
# mispredict counts from Lab Sheet 4 (hazard unit 'forward').
SIM=${1:-./rvsim}
bad=0; runs=0
num() { grep -E "^$1 " "$2" | head -1 | grep -oE '= [0-9]+' | grep -oE '[0-9]+'; }

for f in tests/*.s; do
  $SIM "$f" --batch --single-cycle > ref.txt
  sc=$(grep -o 'cycles = [0-9]*' ref.txt | grep -oE '[0-9]+')
  for h in forward stall; do
    for p in not-taken btfn 1bit 2bit; do
      $SIM "$f" --batch --predictor $p --hazard $h > got.txt; runs=$((runs+1))
      diff <(grep '^x' ref.txt) <(grep '^x' got.txt) > /dev/null || { echo "FAIL regs differ:      $f $p $h"; bad=$((bad+1)); }
      c=$(num cycles got.txt); r=$(num retired got.txt); s=$(num stalls got.txt)
      d=$(num redirects got.txt); m=$(num mispredicts got.txt); j=$(num jalr got.txt)
      [ "$r" = "$sc" ]               || { echo "FAIL retired != oracle:  $f $p $h"; bad=$((bad+1)); }
      [ "$c" = "$((r+4+2*d+s))" ]    || { echo "FAIL cycle identity:     $f $p $h"; bad=$((bad+1)); }
      [ "$d" = "$((m+j))" ]          || { echo "FAIL redirect identity:  $f $p $h"; bad=$((bad+1)); }
    done
  done
done

# golden numbers from the sheet: program  cycles(nt btfn 1bit 2bit)  mispredicts(nt btfn 1bit 2bit)  retired
golden() {
  f=tests/$1.s; [ -f "$f" ] || return
  $SIM "$f" --batch --single-cycle > ref.txt; r=$(grep -o 'cycles = [0-9]*' ref.txt | grep -oE '[0-9]+')
  if [ "$r" != "$7" ]; then echo "note: $1 has $r instructions, not $7 - not the course version, skipping golden check"; return; fi
  set -- "$1" $2 $3 $4 $5 $6
  i=0
  for p in not-taken btfn 1bit 2bit; do
    i=$((i+1)); $SIM "$f" --batch --predictor $p > got.txt
    c=$(num cycles got.txt); m=$(num mispredicts got.txt)
    gc=$(echo "$cyc" | cut -d, -f$i); gm=$(echo "$mis" | cut -d, -f$i)
    [ "$c" = "$gc" ] && [ "$m" = "$gm" ] || { echo "FAIL golden:           $1 $p got $c cycles/$m misses, sheet says $gc/$gm"; bad=$((bad+1)); }
  done
}
g() { cyc=$2; mis=$3; golden "$1" x x x x x "$4"; }
g t1_counted_loop 59,43,45,45 9,1,2,2   37
g t2_nested_loops 78,66,76,70 11,5,10,7 52
g t3_alternating  76,64,74,74 11,5,10,10 50
g t4_calls        278,278,280,278 2,2,3,2 219

echo "$runs runs, $bad problems"
[ $bad -eq 0 ]
