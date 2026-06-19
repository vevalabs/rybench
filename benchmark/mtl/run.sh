#!/usr/bin/env bash
#
# Build the project and run one MTL benchmark property by index.
#
#   benchmark/mtl/run.sh           # list the properties with their indices
#   benchmark/mtl/run.sh <N>       # build everything, recompile schemas, run property N
#
# N is 1-based into the lexicographically-sorted list of property directories
# (those containing a config.toml). e.g. `run.sh 2` runs bounded_absence_before_r.
#
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

mtl_dir="benchmark/mtl"

# Property list: dirs with a config.toml, in glob (lexicographic) order.
props=()
for d in "$mtl_dir"/*/; do
    [ -f "${d}config.toml" ] || continue
    props+=("$(basename "$d")")
done

list_props() {
    local i=1
    for p in "${props[@]}"; do
        printf '  %2d  %s\n' "$i" "$p"
        i=$((i + 1))
    done
}

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <N>"
    echo "Properties:"
    list_props
    exit 1
fi

n="$1"
if ! [[ "$n" =~ ^[0-9]+$ ]] || [ "$n" -lt 1 ] || [ "$n" -gt "${#props[@]}" ]; then
    echo "Index '$n' out of range (1..${#props[@]})."
    echo "Properties:"
    list_props
    exit 1
fi

name="${props[$((n - 1))]}"

# 1. Build everything (configure first if there is no build dir).
[ -d build ] || cmake -S . -B build
cmake --build build

# 2. (Re)compile every schema to .bfbs (what the configs reference).
for d in "$mtl_dir"/*/; do
    [ -f "${d}schema.fbs" ] || continue
    flatc --binary --schema -o "$d" "${d}schema.fbs"
done

# 3. Run the selected property.
echo "=== [$n] $name ==="
export RYBENCH_CONFIG="$mtl_dir"
exec ./build/bin/rybench-filewriter "$name"
