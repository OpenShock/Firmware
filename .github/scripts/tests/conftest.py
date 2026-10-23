import sys
from pathlib import Path

TESTS = Path(__file__).resolve().parent

# The scripts import each other as top-level modules, the way CI runs them: the CI scripts from
# .github/scripts, the build helpers from scripts/. The tests directory itself goes on the path too,
# so test modules share support.py whatever pytest's import mode.
for path in (TESTS.parent, TESTS.parents[2] / 'scripts', TESTS):
    sys.path.insert(0, str(path))
