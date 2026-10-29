#!/usr/bin/env sh
# Puts an installer-registered ESP-IDF on this shell's environment. Source it, from the repository root:
#
#     . scripts/idfenv.sh
#
# Also usable as a CLion toolchain "environment file", so every CMake profile sees ESP-IDF.
# The environment comes from scripts/utils/idfenv.py --dump (KEY=VALUE lines).

# The sourced file's own path: bash and zsh say; plain sh runs it from the repository root.
if [ -n "${BASH_SOURCE:-}" ]; then
  _idfenv_src=$BASH_SOURCE
elif [ -n "${ZSH_VERSION:-}" ]; then
  eval '_idfenv_src=${(%):-%x}'
else
  _idfenv_src=scripts/idfenv.sh
fi
_idfenv_dir=$(CDPATH= cd -- "$(dirname -- "$_idfenv_src")" && pwd)

if _idfenv_dump=$(python3 "$_idfenv_dir/utils/idfenv.py" --dump 2>/dev/null || python "$_idfenv_dir/utils/idfenv.py" --dump); then
  _idfenv_old_ifs=$IFS
  IFS='
'
  for _idfenv_line in $_idfenv_dump; do
    case $_idfenv_line in
      PYTHON=*) ;;
      *=*) export "$_idfenv_line" ;;
    esac
  done
  IFS=$_idfenv_old_ifs
  unset _idfenv_old_ifs _idfenv_line
else
  echo 'idfenv: no ESP-IDF install found - run the ESP-IDF export script instead' >&2
fi
unset _idfenv_src _idfenv_dir _idfenv_dump
