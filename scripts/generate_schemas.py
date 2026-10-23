#!/usr/bin/env python3
"""Generate the TypeScript and C++ flatbuffers sources from schemas/ with flatc."""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from utils.boards import ROOT


def flatc_works(path: str | Path) -> bool:
    try:
        subprocess.run([str(path), '--version'], capture_output=True, check=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


def get_flatc_path():
    flatc_name = 'flatc.exe' if sys.platform == 'win32' else 'flatc'

    # Check in the working directory, then the scripts directory.
    for directory in [Path.cwd(), Path(__file__).resolve().parent]:
        path = directory / flatc_name
        if path.exists() and flatc_works(path):
            return path

    # Fall back to whatever is on PATH.
    if flatc_works(flatc_name):
        return flatc_name

    return None


def generate(flatc_path, language_args, schema_files, output_path, label):
    """Generate into a temporary directory and only replace output_path once flatc succeeded,
    so a failing flatc never leaves the tree without generated sources."""
    output_path.parent.mkdir(parents=True, exist_ok=True)

    tmp = tempfile.mkdtemp(dir=output_path.parent)
    try:
        print(f'Compiling schemas for {label}')
        try:
            subprocess.run([str(flatc_path), *language_args, '-o', tmp, *schema_files], check=True)
        except subprocess.CalledProcessError as err:
            sys.exit(f'flatc failed for {label} (exit code {err.returncode}); {output_path} left unchanged')

        if output_path.exists():
            print(f'Replacing old {label} schemas')
            shutil.rmtree(output_path)
        shutil.move(tmp, output_path)
        tmp = None
    finally:
        if tmp is not None and Path(tmp).exists():
            shutil.rmtree(tmp)


def main() -> int:
    flatc_path = get_flatc_path()
    if flatc_path is None:
        sys.exit('flatc not found. Please install flatc and make sure it is in your PATH.')

    schemas_path = ROOT / 'schemas'
    ts_output_path = ROOT / 'frontend' / 'src' / 'lib' / '_fbs'
    cpp_output_path = ROOT / 'components' / 'serialization' / 'include' / 'serialization' / '_fbs'

    schema_files = [str(p) for p in sorted(schemas_path.rglob('*.fbs')) if not p.name.startswith('Deprecated')]

    if not schema_files:
        sys.exit(f'No .fbs schemas found in {schemas_path}')

    ts_args = [
        # Compile for TypeScript.
        '--ts',
        # Don't add .ts extension to imports.
        '--ts-no-import-ext',
    ]
    cpp_args = [
        # Compile for C++.
        '--cpp',
        # Don't prefix enum values in generated C++ by their enum type.
        '--no-prefix',
        # Use C++11 style scoped and strongly typed enums in generated C++. This also implies --no-prefix.
        '--scoped-enums',
        # Generate type name functions for C++.
        '--gen-name-strings',
        # Don't construct custom string types by passing std::string from Flatbuffers, but (char* + length). This allows efficient construction of custom string types, including zero-copy construction.
        '--cpp-str-flex-ctor',
        # use C++17 features in generated code (experimental).
        '--cpp-std',
        'c++17',
    ]

    generate(flatc_path, ts_args, schema_files, ts_output_path, 'TypeScript')
    generate(flatc_path, cpp_args, schema_files, cpp_output_path, 'C++')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
