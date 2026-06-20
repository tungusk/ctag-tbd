#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  run_long_sdmmc_test.sh [options]

Options:
  --port PORT          Serial port. Default: /dev/cu.usbmodem1101
  --minutes MIN        Total runtime in minutes. Default: 240
  --slot-minutes MIN   Runtime per mode before switching. Default: 30
  --mode MODE          Test mode: both, uhs, or hs. Default: both
  --max-error-runs N   Stop after N monitor runs with errors. Default: 3
  --log-dir DIR        Log directory. Default: tests/sdmmc_stress/logs/<timestamp>
  --idf-path DIR       ESP-IDF path. Default: /Users/rma/esp/esp-idf or $IDF_PATH
  -h, --help           Show this help.

The script builds the selected test images once, then runs:
  1. UHS-I SDR50 100 MHz, 4-bit, phase 2
  2. High-speed 40 MHz, 4-bit, phase 4

All monitor output is appended to full.log. Lines matching common SD/FS/error
patterns are also appended to errors.log for later review.
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
PORT="/dev/cu.usbmodem1101"
TOTAL_MINUTES=240
SLOT_MINUTES=30
TEST_MODE="both"
MAX_ERROR_RUNS=3
LOG_DIR=""
IDF_PATH_ARG="${IDF_PATH:-/Users/rma/esp/esp-idf}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            PORT="$2"
            shift 2
            ;;
        --minutes)
            TOTAL_MINUTES="$2"
            shift 2
            ;;
        --hours)
            TOTAL_MINUTES=$(( $2 * 60 ))
            shift 2
            ;;
        --slot-minutes)
            SLOT_MINUTES="$2"
            shift 2
            ;;
        --mode)
            TEST_MODE="$2"
            shift 2
            ;;
        --max-error-runs)
            MAX_ERROR_RUNS="$2"
            shift 2
            ;;
        --log-dir)
            LOG_DIR="$2"
            shift 2
            ;;
        --idf-path)
            IDF_PATH_ARG="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

timestamp="$(date +%Y%m%d-%H%M%S)"
if [[ -z "$LOG_DIR" ]]; then
    LOG_DIR="$SCRIPT_DIR/logs/$timestamp"
fi

WORK_DIR="${TMPDIR:-/tmp}/sdmmc-longtest-$timestamp"
FULL_LOG="$LOG_DIR/full.log"
ERROR_LOG="$LOG_DIR/errors.log"
SUMMARY_LOG="$LOG_DIR/summary.log"
STATE_LOG="$LOG_DIR/state.log"
IGNORED_LOG="$LOG_DIR/ignored-known-benign.log"
ROLLING_LINES=10
STATE_LINES=8
ROLLING_WINDOW_ACTIVE=0
ROLLING_TITLE=""
ROLLING_INPUT=""
ERROR_PATTERN="(^|[[:space:]])(E \\(|W \\(|Error|error|failed|FAILED|timeout|0x107|0x109|SD_TRANS|diskio_sdmmc)"
KNOWN_BENIGN_PATTERN="(SD_HOST: sd_host_del_sdmmc_controller\\([0-9]+\\): host controller with slot registered|spi_flash: Detected size\\([0-9]+k\\) larger than the size in the binary image header\\([0-9]+k\\)|sdmmc_stress: Rebooting after [0-9]+ successful cycles as configured)"

mkdir -p "$LOG_DIR" "$WORK_DIR"

if [[ ! -f "$IDF_PATH_ARG/export.sh" ]]; then
    echo "ESP-IDF export.sh not found at: $IDF_PATH_ARG/export.sh" >&2
    exit 1
fi

# shellcheck source=/dev/null
source "$IDF_PATH_ARG/export.sh" >/dev/null

slot_seconds=$((SLOT_MINUTES * 60))
total_seconds=$((TOTAL_MINUTES * 60))
if [[ "$slot_seconds" -le 0 || "$total_seconds" -le 0 ]]; then
    echo "--minutes and --slot-minutes must be positive" >&2
    exit 2
fi
if [[ "$MAX_ERROR_RUNS" -lt 0 ]]; then
    echo "--max-error-runs must be zero or positive" >&2
    exit 2
fi
if [[ "$TEST_MODE" != "both" && "$TEST_MODE" != "uhs" && "$TEST_MODE" != "hs" ]]; then
    echo "--mode must be one of: both, uhs, hs" >&2
    exit 2
fi

write_common_defaults() {
    local file="$1"
    cat > "$file" <<'EOF'
CONFIG_IDF_TARGET="esp32p4"
CONFIG_PARTITION_TABLE_SINGLE_APP=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_FATFS_IMMEDIATE_FSYNC=y
CONFIG_FATFS_USE_DYN_BUFFERS=n
CONFIG_ESP_WIFI_REMOTE_ENABLED=n
CONFIG_ESP_HOSTED_ENABLED=n
CONFIG_TEST_SDMMC_POWER_CYCLE=y
CONFIG_TEST_SDMMC_FORCE_3V3_BEFORE_POWER_CYCLE=y
CONFIG_TEST_SDMMC_SETTLE_MS=500
CONFIG_TEST_SDMMC_EXTRACT_ZIP_AT_BOOT=y
CONFIG_TEST_SDMMC_READ_ZIP_BENCHMARK=y
CONFIG_TEST_SDMMC_USE_BOUNCE_WRAPPER=y
CONFIG_TEST_SDMMC_REBOOT_AFTER_CYCLES=3
EOF
}

write_uhs_defaults() {
    local file="$1"
    write_common_defaults "$file"
    cat >> "$file" <<'EOF'
CONFIG_TEST_SDMMC_TIMING_SDR50_4BIT_PHASE2=y
CONFIG_TEST_SDMMC_RETRY_NON_UHS_MOUNTS=2
EOF
}

write_hs_defaults() {
    local file="$1"
    write_common_defaults "$file"
    cat >> "$file" <<'EOF'
CONFIG_TEST_SDMMC_TIMING_HS_4BIT=y
CONFIG_TEST_SDMMC_HS_DELAY_PHASE=4
EOF
}

append_error_lines() {
    local input="$1"
    perl -pe 's/\e\[[0-9;]*[A-Za-z]//g' "$input" \
        | grep -E "$ERROR_PATTERN" \
        | grep -Ev "$KNOWN_BENIGN_PATTERN" >> "$ERROR_LOG" || true
}

append_ignored_lines() {
    local input="$1"
    perl -pe 's/\e\[[0-9;]*[A-Za-z]//g' "$input" \
        | grep -E "$KNOWN_BENIGN_PATTERN" >> "$IGNORED_LOG" || true
}

count_error_lines() {
    local input="$1"
    perl -pe 's/\e\[[0-9;]*[A-Za-z]//g' "$input" \
        | grep -E "$ERROR_PATTERN" \
        | grep -Ev "$KNOWN_BENIGN_PATTERN" \
        | wc -l \
        | tr -d ' '
}

count_ignored_lines() {
    local input="$1"
    perl -pe 's/\e\[[0-9;]*[A-Za-z]//g' "$input" \
        | grep -E "$KNOWN_BENIGN_PATTERN" \
        | wc -l \
        | tr -d ' '
}

cleanup_terminal() {
    if [[ -t 1 ]]; then
        printf '\033[?25h'
    fi
}

trap cleanup_terminal EXIT

render_rolling_window() {
    local title="$1"
    local input="$2"

    if [[ ! -t 1 ]]; then
        return
    fi

    ROLLING_TITLE="$title"
    ROLLING_INPUT="$input"
    ROLLING_WINDOW_ACTIVE=1

    printf '\033[?25l'
    printf '\033[H\033[J'
    printf -- 'SDMMC long test\n'
    printf -- 'State log: %s\n' "$STATE_LOG"
    printf -- 'Full log:  %s\n' "$FULL_LOG"
    printf -- 'Errors:    %s\n' "$ERROR_LOG"
    printf -- 'Ignored:   %s\n' "$IGNORED_LOG"
    printf -- '\n'
    printf -- '--- State ---\n'
    if [[ -f "$STATE_LOG" ]]; then
        tail -n "$STATE_LINES" "$STATE_LOG" | awk -v n="$STATE_LINES" '{ print; ++c } END { for (; c < n; ++c) print "" }' || true
    else
        awk -v n="$STATE_LINES" 'END { for (c = 0; c < n; ++c) print "" }' < /dev/null
    fi
    printf -- '\n'
    printf -- '--- %s ---\n' "$title"
    if [[ -f "$input" ]]; then
        tail -n "$ROLLING_LINES" "$input" | awk -v n="$ROLLING_LINES" '{ print; ++c } END { for (; c < n; ++c) print "" }' || true
    else
        awk -v n="$ROLLING_LINES" 'END { for (c = 0; c < n; ++c) print "" }' < /dev/null
    fi
    printf -- 'Log: %s\n' "$input"
}

finish_rolling_window() {
    local title="$1"
    local input="$2"

    render_rolling_window "$title" "$input"
    cleanup_terminal
}

run_with_rolling_log() {
    local title="$1"
    local output="$2"
    shift 2

    "$@" > "$output" 2>&1 &
    local cmd_pid=$!
    while kill -0 "$cmd_pid" 2>/dev/null; do
        render_rolling_window "$title" "$output"
        sleep 1
    done

    local status=0
    if wait "$cmd_pid"; then
        status=0
    else
        status=$?
    fi
    finish_rolling_window "$title" "$output"
    return "$status"
}

log_msg() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] $*"
    echo "$msg" >> "$SUMMARY_LOG"
    echo "$msg" >> "$FULL_LOG"
    echo "$msg" >> "$STATE_LOG"
    if [[ -t 1 ]]; then
        if [[ "$ROLLING_WINDOW_ACTIVE" -eq 1 ]]; then
            render_rolling_window "$ROLLING_TITLE" "$ROLLING_INPUT"
        else
            printf -- '%s\n' "$msg"
        fi
    else
        printf -- '%s\n' "$msg"
    fi
}

build_mode() {
    local mode="$1"
    local defaults="$2"
    local build_dir="$3"
    local sdkconfig="$4"
    local build_log="$LOG_DIR/build-$mode.log"

    log_msg "Building $mode image"
    if run_with_rolling_log "Building $mode image" "$build_log" \
        idf.py -C "$PROJECT_DIR" \
        -B "$build_dir" \
        -DSDKCONFIG="$sdkconfig" \
        -DSDKCONFIG_DEFAULTS="$defaults;$SCRIPT_DIR/sdkconfig.defaults.hosted" \
        build; then
        cat "$build_log" >> "$FULL_LOG"
        append_error_lines "$build_log"
        append_ignored_lines "$build_log"
        log_msg "Build $mode OK"
    else
        cat "$build_log" >> "$FULL_LOG"
        append_error_lines "$build_log"
        append_ignored_lines "$build_log"
        log_msg "Build $mode FAILED"
        exit 1
    fi
}

flash_mode() {
    local mode="$1"
    local build_dir="$2"
    local sdkconfig="$3"
    local defaults="$4"
    local flash_log="$LOG_DIR/flash-$mode-$(date +%H%M%S).log"

    log_msg "Flashing $mode image"
    if run_with_rolling_log "Flashing $mode image" "$flash_log" \
        idf.py -C "$PROJECT_DIR" \
        -B "$build_dir" \
        -DSDKCONFIG="$sdkconfig" \
        -DSDKCONFIG_DEFAULTS="$defaults;$SCRIPT_DIR/sdkconfig.defaults.hosted" \
        -p "$PORT" flash; then
        cat "$flash_log" >> "$FULL_LOG"
        append_error_lines "$flash_log"
        append_ignored_lines "$flash_log"
        log_msg "Flash $mode OK"
    else
        cat "$flash_log" >> "$FULL_LOG"
        append_error_lines "$flash_log"
        append_ignored_lines "$flash_log"
        log_msg "Flash $mode FAILED"
        exit 1
    fi
}

monitor_mode() {
    local mode="$1"
    local build_dir="$2"
    local sdkconfig="$3"
    local defaults="$4"
    local seconds="$5"
    local monitor_log="$LOG_DIR/monitor-$mode-$(date +%H%M%S).log"

    log_msg "Monitoring $mode for ${seconds}s"
    run_with_rolling_log "Monitoring $mode" "$monitor_log" \
        python "$SCRIPT_DIR/capture_serial.py" \
        --port "$PORT" \
        --baud 115200 \
        --duration "$seconds"

    cat "$monitor_log" >> "$FULL_LOG"
    append_error_lines "$monitor_log"
    append_ignored_lines "$monitor_log"
    local error_count
    local ignored_count
    error_count="$(count_error_lines "$monitor_log")"
    ignored_count="$(count_ignored_lines "$monitor_log")"
    if [[ "$error_count" -gt 0 ]]; then
        log_msg "Monitor $mode complete with ${error_count} error/warning lines, ${ignored_count} ignored known-benign lines"
        return 1
    fi

    log_msg "Monitor $mode complete without matched errors, ${ignored_count} ignored known-benign lines"
    return 0
}

UHS_DEFAULTS="$WORK_DIR/uhs.defaults"
HS_DEFAULTS="$WORK_DIR/hs.defaults"
UHS_BUILD="$WORK_DIR/build-uhs"
HS_BUILD="$WORK_DIR/build-hs"
UHS_SDKCONFIG="$WORK_DIR/uhs.sdkconfig"
HS_SDKCONFIG="$WORK_DIR/hs.sdkconfig"

write_uhs_defaults "$UHS_DEFAULTS"
write_hs_defaults "$HS_DEFAULTS"

log_msg "SDMMC long test started"
log_msg "Port: $PORT"
log_msg "Total runtime: ${TOTAL_MINUTES}min; slot length: ${SLOT_MINUTES}min"
log_msg "Mode: $TEST_MODE"
log_msg "Stop after error runs: $MAX_ERROR_RUNS"
log_msg "Logs: $LOG_DIR"
log_msg "Work dir: $WORK_DIR"

if [[ "$TEST_MODE" == "both" || "$TEST_MODE" == "uhs" ]]; then
    build_mode "uhs" "$UHS_DEFAULTS" "$UHS_BUILD" "$UHS_SDKCONFIG"
fi
if [[ "$TEST_MODE" == "both" || "$TEST_MODE" == "hs" ]]; then
    build_mode "hs" "$HS_DEFAULTS" "$HS_BUILD" "$HS_SDKCONFIG"
fi

start_epoch="$(date +%s)"
end_epoch=$((start_epoch + total_seconds))
slot_index=0
error_runs=0

while [[ "$(date +%s)" -lt "$end_epoch" ]]; do
    if [[ "$TEST_MODE" == "uhs" ]]; then
        mode="uhs"
        build_dir="$UHS_BUILD"
        sdkconfig="$UHS_SDKCONFIG"
        defaults="$UHS_DEFAULTS"
    elif [[ "$TEST_MODE" == "hs" ]]; then
        mode="hs"
        build_dir="$HS_BUILD"
        sdkconfig="$HS_SDKCONFIG"
        defaults="$HS_DEFAULTS"
    elif (( slot_index % 2 == 0 )); then
        mode="uhs"
        build_dir="$UHS_BUILD"
        sdkconfig="$UHS_SDKCONFIG"
        defaults="$UHS_DEFAULTS"
    else
        mode="hs"
        build_dir="$HS_BUILD"
        sdkconfig="$HS_SDKCONFIG"
        defaults="$HS_DEFAULTS"
    fi

    now="$(date +%s)"
    remaining=$((end_epoch - now))
    run_seconds="$slot_seconds"
    if [[ "$remaining" -lt "$run_seconds" ]]; then
        run_seconds="$remaining"
    fi

    flash_mode "$mode" "$build_dir" "$sdkconfig" "$defaults"
    if monitor_mode "$mode" "$build_dir" "$sdkconfig" "$defaults" "$run_seconds"; then
        :
    else
        error_runs=$((error_runs + 1))
        log_msg "Error run count: ${error_runs}/${MAX_ERROR_RUNS}"
        if [[ "$MAX_ERROR_RUNS" -gt 0 && "$error_runs" -ge "$MAX_ERROR_RUNS" ]]; then
            log_msg "Stopping after ${error_runs} monitor runs with matched errors"
            break
        fi
    fi

    slot_index=$((slot_index + 1))
done

log_msg "SDMMC long test finished"
log_msg "Full log: $FULL_LOG"
log_msg "Error log: $ERROR_LOG"
