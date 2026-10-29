"""Small file helpers shared by the build scripts."""

import os
import tempfile
from pathlib import Path


def write_if_changed(path: Path, content: str) -> bool:
    """Write only when the text differs, returning whether it was written.

    CMake re-runs the generators on every configure, and idf.py reconfigures when an
    input's mtime moves, so rewriting identical content would disturb ninja and ccache
    on incremental builds for nothing.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding='utf-8') == content:
        return False
    path.write_text(content, encoding='utf-8', newline='\n')
    return True


def atomic_write_bytes(path: Path, data: bytes) -> None:
    """Replace path with data in one step, so an interrupted write never leaves a truncated file behind."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=f'.{path.name}.', suffix='.tmp', dir=path.parent)
    tmp = Path(tmp_name)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
        tmp.replace(path)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise
