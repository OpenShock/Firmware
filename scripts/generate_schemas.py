import os
import sys
import shutil

# Check if flatc is installed.
if os.system('flatc --version') != 0:
    print('flatc not found. Please install flatc and make sure it is in your PATH.')
    exit(1)


def resolve_path(path):
    cwd = os.getcwd()
    script_path = os.path.dirname(os.path.realpath(__file__))
    return os.path.relpath(os.path.join(script_path, path).replace('\\', '/'), cwd)


# resolve paths
schemas_path = resolve_path('../schemas')
ts_output_path = resolve_path('../WebUI/src/lib/fbs')
cpp_output_path = resolve_path('../include/fbs')

# Get all the schema files.
schema_files = []
for root, dirs, files in os.walk(schemas_path):
    root = root.replace('\\', '/')
    for filename in files:
        filepath = os.path.join(root, filename)
        if filename.endswith('.fbs'):
            schema_files.append(filepath)

# Compile the schemas for C++ and TypeScript.
flatc_args_ts = [
    # Compile for TypeScript.
    '--ts',
    # Output directory.
    '-o ' + ts_output_path,
] + schema_files
flatc_args_cpp = [
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
    '--cpp-std c++17',
    # Output directory.
    '-o ' + cpp_output_path,
] + schema_files

if os.path.exists(ts_output_path):
    print('Deleting old TypeScript schemas')
    shutil.rmtree(ts_output_path)
print('Compiling schemas for TypeScript')
os.system('flatc ' + ' '.join(flatc_args_ts))

if os.path.exists(cpp_output_path):
    print('Deleting old C++ schemas')
    shutil.rmtree(cpp_output_path)
print('Compiling schemas for C++')
os.system('flatc ' + ' '.join(flatc_args_cpp))
