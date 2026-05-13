#!/usr/bin/env bash
# ===========================================================================================
# dev-watch.sh — file-change rebuild loop for the TBD-16 simulator.
#
# Watches the rack-machine sources (and the GrooveBoxRack host file) for changes and
# re-runs the fastest useful build + test command on each modification.  The intended
# workflow is:
#
#   1. In one terminal:  tools/dev-watch.sh [--machine <id>]
#   2. In another:       edit  components/ctagSoundProcessor/rack/RackMyVoice.cpp
#   3. Watch this terminal: it rebuilds + runs the test in <2 s and prints peak values.
#
# By default it runs `make routing-test load-test` and then `./routing-test` — catches
# both "you broke compilation" and "you broke routing" in one loop.  With `--machine <id>`
# it runs `./load-test --machine <id>` instead, which isolates that voice and reports its
# dry / FX-bus peak (best for "I'm tuning a single voice's DSP").
#
# Requires `fswatch` on macOS (`brew install fswatch`) or `inotifywait` on Linux
# (`apt install inotify-tools`).  The script auto-picks whichever is available.
#
# Stop with Ctrl-C.
# ===========================================================================================

set -u  # treat unset variables as errors; intentional unset is `${var:-}`

# Resolve repo root from this script's location so `cd` is robust.
script_dir="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
repo_root="$( cd -- "${script_dir}/.." &> /dev/null && pwd )"
build_dir="${repo_root}/simulator/build"
watch_dirs=(
    "${repo_root}/components/ctagSoundProcessor/rack"
    "${repo_root}/components/ctagSoundProcessor/ctagSoundProcessorGrooveBoxRack.cpp"
    "${repo_root}/components/ctagSoundProcessor/ctagSoundProcessorGrooveBoxRack.hpp"
    "${repo_root}/simulator/tests/test_routing.cpp"
    "${repo_root}/simulator/tests/test_loadprocessors.cpp"
)

# Parse: --machine <id> switches to single-voice isolation mode.
machine_id=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --machine) machine_id="$2"; shift 2;;
        -h|--help) sed -n '2,/^# ====/p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
        *) echo "unknown arg: $1 (try --help)" >&2; exit 2;;
    esac
done

# Pick a file-watcher we can actually run.
watcher=""
if command -v fswatch >/dev/null 2>&1; then
    watcher="fswatch"
elif command -v inotifywait >/dev/null 2>&1; then
    watcher="inotifywait"
else
    echo "neither fswatch nor inotifywait is installed — install one:" >&2
    echo "  macOS:  brew install fswatch" >&2
    echo "  Linux:  sudo apt install inotify-tools" >&2
    exit 2
fi

# Sanity-check the build dir exists.
if [[ ! -f "${build_dir}/Makefile" ]]; then
    echo "no Makefile in ${build_dir} — run the simulator bootstrap first:" >&2
    echo "  cd simulator && mkdir -p build && cd build && cmake .. && make" >&2
    exit 2
fi

# Detect colour-capable stdout.
if [[ -t 1 ]]; then  c_g=$'\e[32m'; c_r=$'\e[31m'; c_y=$'\e[33m'; c_n=$'\e[0m'
else                 c_g=""; c_r=""; c_y=""; c_n=""; fi

run_once() {
    local stamp; stamp="$(date +%H:%M:%S)"
    echo
    echo "${c_y}[${stamp}] rebuilding…${c_n}"
    # Build the two binaries the iteration loop cares about.
    if ! ( cd "${build_dir}" && make routing-test load-test ) 2>&1 | tail -8; then
        echo "${c_r}[${stamp}] build FAILED${c_n}"
        return
    fi

    if [[ -n "${machine_id}" ]]; then
        # Single-voice isolation mode — fast: only loads the rack and fires one note.
        ( cd "${build_dir}" && ./load-test --machine "${machine_id}" ) \
            | grep -E "dry peak|FX2-bus|AUDIBLE|SILENT|FX BUS|unknown" \
            || echo "${c_r}[${stamp}] load-test --machine produced no output${c_n}"
    else
        # Default: regression test + a smoke run.
        ( cd "${build_dir}" && ./routing-test ) | grep -E "PASS|FAIL"
        ( cd "${build_dir}" && ./load-test GrooveBoxRack 2>&1 ) \
            | grep -E "AUDIBLE|FX BUS WORKS|SAMPLES PLAY|crash" \
            || true
    fi
    echo "${c_g}[${stamp}] done — watching for next change${c_n}"
}

echo "dev-watch: watching ${#watch_dirs[@]} paths (via ${watcher})"
if [[ -n "${machine_id}" ]]; then
    echo "          mode: --machine ${machine_id}  (single-voice isolation)"
else
    echo "          mode: full (routing-test + load-test GrooveBoxRack)"
fi
echo "          Ctrl-C to stop."

# Initial build, so you know the baseline state before any edit.
run_once

# Watch loop.  Both fswatch and inotifywait emit one path per change; we don't
# care which path — any change re-runs the build.  Debounce 300 ms-ish by
# discarding events that arrive within the same iteration.
case "${watcher}" in
    fswatch)
        fswatch -o "${watch_dirs[@]}" 2>/dev/null | while read -r _; do run_once; done
        ;;
    inotifywait)
        # -m: monitor (don't exit after first event); -e modify,create,close_write,move
        inotifywait -m -r -e modify,create,close_write,move "${watch_dirs[@]}" 2>/dev/null \
            | while read -r _; do run_once; done
        ;;
esac
