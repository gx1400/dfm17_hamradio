#!/bin/sh
# Read and decode the hw_survey results after a HW_SURVEY build has run.
#   usage: read_survey.sh path/to/dfm17.elf
. "$(dirname "$0")/common.sh"
ELF=${1:?usage: $0 dfm17.elf}
A=$(sym "$ELF" hw_survey)
[ -n "$A" ] || { echo "hw_survey not in $ELF (build with HW_SURVEY defined)" >&2; exit 1; }
readmem "$A" 712 | python3 -I "$(dirname "$0")/decode_survey.py"
