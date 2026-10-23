"""Small file helpers shared by the build scripts."""

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
