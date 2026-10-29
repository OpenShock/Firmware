#!/usr/bin/env python3
"""Generate the TypeScript and C++ flatbuffers sources from schemas/ with flatc.

flatc is taken from scripts/ (where fetch_flatc.py puts the version the runtime needs), else from PATH.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

from utils.paths import ROOT

SCRIPTS_DIR = Path(__file__).resolve().parent
FLATC_NAME = 'flatc.exe' if sys.platform == 'win32' else 'flatc'

SCHEMAS_DIR = ROOT / 'schemas'
TS_OUTPUT = ROOT / 'frontend' / 'src' / 'lib' / '_fbs'
CPP_OUTPUT = ROOT / 'components' / 'serialization' / 'include' / 'serialization' / '_fbs'

TS_ARGS = [
    # Compile for TypeScript.
    '--ts',
    # Don't add .ts extension to imports.
    '--ts-no-import-ext',
]
CPP_ARGS = [
    # Compile for C++.
    '--cpp',
    # Don't prefix enum values in generated C++ by their enum type.
    '--no-prefix',
    # Use C++11 style scoped and strongly typed enums in generated C++. This also implies --no-prefix.
    '--scoped-enums',
    # Generate type name functions for C++.
    '--gen-name-strings',
    # Don't construct custom string types by passing std::string from Flatbuffers, but (char* + length).
    # This allows efficient construction of custom string types, including zero-copy construction.
    '--cpp-str-flex-ctor',
    # use C++17 features in generated code (experimental).
    '--cpp-std',
    'c++17',
]


def flatc_problem(flatc: str | Path) -> str | None:
    """Why this flatc cannot run, or None when `flatc --version` works."""
    try:
        subprocess.run([str(flatc), '--version'], capture_output=True, check=True)
    except FileNotFoundError:
        return 'not found'
    except PermissionError:
        return 'found but not executable (chmod +x it, or re-run fetch_flatc.py)'
    except OSError as err:
        return f'cannot run: {err}'
    except subprocess.CalledProcessError as err:
        return f'`--version` exited with {err.returncode}'
    return None


def find_flatc() -> str | Path:
    """The pinned flatc in scripts/, else the one on PATH; fails naming why each was rejected."""
    problems = []
    for candidate in (SCRIPTS_DIR / FLATC_NAME, FLATC_NAME):
        problem = flatc_problem(candidate)
        if problem is None:
            return candidate
        problems.append(f'{candidate}: {problem}')
    raise SystemExit(
        'error: no working flatc.\n  ' + '\n  '.join(problems) + '\nRun python scripts/fetch_flatc.py to fetch it.'
    )


def generate(
    flatc: str | Path, language_args: list[str], schema_files: list[str], output_path: Path, label: str
) -> None:
    """Generate into a temporary directory and swap it in only once flatc succeeded.

    The old tree is moved aside rather than deleted first, and moved back if the new one cannot take
    its place, so no failure leaves the tree without generated sources.
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output_path.parent) as tmp:
        staged = Path(tmp) / 'new'
        staged.mkdir()
        print(f'Compiling schemas for {label}')
        try:
            subprocess.run([str(flatc), *language_args, '-o', str(staged), *schema_files], check=True)
        except subprocess.CalledProcessError as err:
            raise SystemExit(
                f'error: flatc failed for {label} (exit code {err.returncode}); {output_path} left unchanged'
            ) from None

        old = Path(tmp) / 'old'
        if output_path.exists():
            print(f'Replacing old {label} schemas')
            output_path.rename(old)
        try:
            staged.rename(output_path)
        except OSError:
            if old.exists():
                old.rename(output_path)
            raise


def main() -> int:
    flatc = find_flatc()

    schema_files = [str(p) for p in sorted(SCHEMAS_DIR.rglob('*.fbs')) if not p.name.startswith('Deprecated')]
    if not schema_files:
        raise SystemExit(f'error: no .fbs schemas found in {SCHEMAS_DIR}')

    generate(flatc, TS_ARGS, schema_files, TS_OUTPUT, 'TypeScript')
    generate(flatc, CPP_ARGS, schema_files, CPP_OUTPUT, 'C++')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
