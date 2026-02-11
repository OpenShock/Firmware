#!/bin/bash
FILE_PATH=$(jq -r '.tool_input.file_path // empty')

if [[ "$FILE_PATH" =~ \.(cpp|c|h|hpp|cc|cxx)$ ]] && [[ "$FILE_PATH" != */_fbs/* ]]; then
  npx clang-format -i "$FILE_PATH"
fi

exit 0
