#!/bin/bash
# run_until.sh — run a command, print its output, and stop it gracefully
#                when a pattern appears on its output.
# Usage: ./run_until.sh <pattern> <command> [args...]
#
# Example:
#   ./run_until.sh "login:" ./bin/sparcv8_leon_smp ...

set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <pattern> <command> [args...]" >&2
    exit 1
fi

PATTERN="$1"; shift

FIFO=$(mktemp -u /tmp/run_until.XXXXXX)
mkfifo "$FIFO"
trap "rm -f '$FIFO'; stty sane 2>/dev/null || true" EXIT

start_ms=$(date +%s%3N)

# Start the emulator with its output going to the fifo.
# Explicitly give it /dev/tty as stdin so tcgetattr() succeeds —
# bash redirects stdin to /dev/null for background jobs in non-interactive shells.
"$@" > "$FIFO" 2>&1 < /dev/tty &
CMD_PID=$!

# Open the fifo on fd 3 and keep it open for the duration
exec 3< "$FIFO"

# Read all output, print it, and watch for the pattern
matched=0
while IFS= read -r -u3 line; do
    printf '%s\n' "$line"
    if [[ $matched -eq 0 && "$line" == *"$PATTERN"* ]]; then
        end_ms=$(date +%s%3N)
        elapsed=$(( end_ms - start_ms ))
        matched=1
        # Send SIGINT — same as Ctrl+C — so the emulator runs its signal handler
        kill -INT $CMD_PID 2>/dev/null
    fi
done

exec 3<&-

stty sane 2>/dev/null || true

if [[ $matched -eq 1 ]]; then
    printf '\n** Matched: "%s"\n' "$PATTERN"
    printf '** Wall time: %d.%03d s\n' $(( elapsed / 1000 )) $(( elapsed % 1000 ))
fi
