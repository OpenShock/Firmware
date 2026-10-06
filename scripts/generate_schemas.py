import os
import shutil
import subprocess
import sys
import tempfile


def flatc_works(path: str) -> bool:
    try:
        return subprocess.run([path, '--version'], capture_output=True).returncode == 0
    except OSError:
        return False


def get_flatc_path():
    script_dir = os.path.dirname(os.path.realpath(__file__))

    # Pick the right binary name for the platform.
    flatc_name = 'flatc.exe' if sys.platform == 'win32' else 'flatc'

    # Check in the working directory, then the scripts directory.
    for directory in [os.getcwd(), script_dir]:
        path = os.path.join(directory, flatc_name)
        if os.path.exists(path) and flatc_works(path):
            return path

    # Fall back to whatever is on PATH.
    if flatc_works(flatc_name):
        return flatc_name

    return None


def resolve_path(path):
    script_path = os.path.dirname(os.path.realpath(__file__))
    return os.path.normpath(os.path.join(script_path, path))


def generate(flatc_path, language_args, schema_files, output_path, label):
    """Generate into a temporary directory and only replace output_path once flatc succeeded,
    so a failing flatc never leaves the tree without generated sources."""
    parent = os.path.dirname(output_path)
    os.makedirs(parent, exist_ok=True)

    tmp = tempfile.mkdtemp(dir=parent)
    try:
        print(f'Compiling schemas for {label}')
        result = subprocess.run([flatc_path, *language_args, '-o', tmp, *schema_files])
        if result.returncode != 0:
            sys.exit(f'flatc failed for {label} (exit code {result.returncode}); {output_path} left unchanged')

        if os.path.exists(output_path):
            print(f'Replacing old {label} schemas')
            shutil.rmtree(output_path)
        shutil.move(tmp, output_path)
        tmp = None
    finally:
        if tmp is not None and os.path.exists(tmp):
            shutil.rmtree(tmp)


def main():
    flatc_path = get_flatc_path()
    if flatc_path is None:
        sys.exit('flatc not found. Please install flatc and make sure it is in your PATH.')

    schemas_path = resolve_path('../schemas')
    ts_output_path = resolve_path('../frontend/src/lib/_fbs')
    cpp_output_path = resolve_path('../components/serialization/include/serialization/_fbs')

    # Get all the schema files.
    schema_files = []
    for root, _, files in os.walk(schemas_path):
        for filename in sorted(files):
            if filename.endswith('.fbs') and not filename.startswith('Deprecated'):
                schema_files.append(os.path.join(root, filename))

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


if __name__ == '__main__':
    main()
