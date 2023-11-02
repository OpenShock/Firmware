import importlib.util
import sys
import subprocess

# This function checks if a package is installed using multiple methods
def is_installed(package):
    if package in sys.modules:
        return True

    if importlib.util.find_spec(package) is not None:
        return True

    try:
        __import__(package)
        return True
    except ImportError:
        pass

    return False

# Dictionary of required packages and their pip install names
required = {
    'fontTools': 'fonttools[woff,unicode]',
    'brotli': 'brotli',
}

# Get all packages that are not installed
to_install = [ required[pip] for pip in required if not is_installed(pip) ]

# Install all packages that are not installed
if len(to_install) > 0:
    print('Installing: ' + ', '.join(to_install))

    # Get the python executable path
    python_path = sys.executable

    # Install the packages
    subprocess.check_call([python_path, '-m', 'pip', 'install', *to_install])
