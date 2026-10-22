import sys
from pathlib import Path

# The scripts import each other as top-level modules, the way CI runs them.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
