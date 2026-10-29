#!/usr/bin/env bash
# Exports a restored ESP-IDF install (EIM's activate_idf_<version>.sh) to later steps of this job.
set -euo pipefail

act="$(compgen -G '/tmp/esp/activate_idf_*.sh' || compgen -G '/opt/esp/activate_idf_*.sh' \
  || compgen -G "$HOME/.espressif/activate_idf_*.sh" || compgen -G "$HOME/.espressif/tools/activate_idf_*.sh" || true)"
act="$(printf '%s\n' "$act" | head -1)"
if [ -z "$act" ]; then
  echo '::error::ESP-IDF cache was restored but no activate_idf_*.sh was found in it.'
  exit 1
fi
echo "activating $act"

# -e prints KEY=VALUE lines; sourcing the script refuses to run inside a step.
env_dump="$(sh "$act" -e)"
if [ -z "$env_dump" ]; then
  echo "::error::$act produced no environment."
  exit 1
fi

# PATH= holds only the directories to prepend; SYSTEM_PATH is the caller's own PATH.
sed -n 's/^PATH=//p' <<< "$env_dump" | tr ':' '\n' | grep -v '^$' >> "$GITHUB_PATH" || true
grep -E '^[^=]+=' <<< "$env_dump" | grep -vE '^(PATH|SYSTEM_PATH)=' >> "$GITHUB_ENV" || true

idf_path="$(sed -n 's/^IDF_PATH=//p' <<< "$env_dump" | head -1)"
if [ -z "$idf_path" ]; then
  echo "::error::$act did not report IDF_PATH."
  exit 1
fi
# idf.py lives in $IDF_PATH/tools, which the script does not emit.
echo "$idf_path/tools" >> "$GITHUB_PATH"
echo "activated ESP-IDF at $idf_path"
