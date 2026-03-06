import os
import shutil
import subprocess


def flatc_test(path: str):
    return os.system(path + ' --version') == 0


def exec_silent(args: list[str]):
    return subprocess.check_output(args, shell=True).decode('utf-8').strip()


def get_flatc_path():
    script_dir = os.path.dirname(os.path.realpath(__file__))

    # Check CWD and scripts dir for both flatc and flatc.exe.
    for candidate_name in ('flatc', 'flatc.exe'):
        for search_dir in (os.getcwd(), script_dir):
            path = os.path.join(search_dir, candidate_name)
            if os.path.exists(path):
                if flatc_test(path):
                    return path

    # Fall back to a globally installed flatc.
    if flatc_test('flatc'):
        return 'flatc'

    return None


flatc_path = get_flatc_path()
if flatc_path is None:
    print('flatc not found. Please install flatc and make sure it is in your PATH.')
    exit(1)


def run_flatc(args):
    # Run flatc.
    return os.system(flatc_path + ' ' + args)


def resolve_path(path):
    cwd = os.getcwd()
    script_path = os.path.dirname(os.path.realpath(__file__))
    return os.path.relpath(os.path.join(script_path, path).replace('\\', '/'), cwd)


# resolve paths
schemas_path = resolve_path('../schemas')
ts_output_path = resolve_path('../frontend/src/lib/_fbs')
cpp_output_path = resolve_path('../include/serialization/_fbs')
go_output_path = resolve_path('../tools/mock-portal/fbs')
go_module_name = 'openshock.dev/mock-portal/fbs'

# Get all the schema files.
schema_files = []
for root, dirs, files in os.walk(schemas_path):
    root = root.replace('\\', '/')
    for filename in files:
        filepath = os.path.join(root, filename)
        if filename.endswith('.fbs') and not filename.startswith('Deprecated'):
            schema_files.append(filepath)

# Compile the schemas for C++ and TypeScript.
flatc_args_ts = [
    # Compile for TypeScript.
    '--ts',
    # Don't add .ts extension to imports.
    '--ts-no-import-ext',
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
flatc_args_go = [
    # Compile for Go.
    '--go',
    # Prefix local imports with the module name so generated files work within the module.
    '--go-module-name ' + go_module_name,
    # Output directory.
    '-o ' + go_output_path,
] + schema_files

if os.path.exists(ts_output_path):
    print('Deleting old TypeScript schemas')
    shutil.rmtree(ts_output_path)
print('Compiling schemas for TypeScript')
run_flatc(' '.join(flatc_args_ts))

if os.path.exists(cpp_output_path):
    print('Deleting old C++ schemas')
    shutil.rmtree(cpp_output_path)
print('Compiling schemas for C++')
run_flatc(' '.join(flatc_args_cpp))

if os.path.exists(go_output_path):
    print('Deleting old Go schemas')
    shutil.rmtree(go_output_path)
print('Compiling schemas for Go')
run_flatc(' '.join(flatc_args_go))
