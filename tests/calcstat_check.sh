#!/bin/sh
# Compare the rating code (nfsuserver/calcstat.cpp) with the 6,000-line
# CalcStat it replaced, for every combination of inputs.
#
#   tests/calcstat_check.sh [COMMIT]
#
# COMMIT is the last one with the old code (default d0231ec). Needs a C++
# compiler; on NixOS: nix shell nixpkgs#gcc --command tests/calcstat_check.sh
set -e
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git show "${1:-d0231ec}:nfsuserver/nfsuserver.cpp" \
	| sed -n '/^void CalcStat(/,/^}/p' | sed '1s/CalcStat(/CalcStatOld(/' > "$work/old_calcstat.inc"
${CXX:-c++} -O2 -w -I nfsuserver -I "$work" -o "$work/check" tests/calcstat_check.cpp nfsuserver/calcstat.cpp
"$work/check"
