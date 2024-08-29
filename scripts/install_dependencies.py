import importlib.util
import sys
import subprocess

class Package:
    def __init__(self, pip_name, pip_options, import_name):
        self.pip_name = pip_name
        self.pip_options = pip_options
        self.import_name = import_name

    def pip_name(self):
        return self.pip_name

    def pip_package(self):
        if len(self.pip_options) == 0:
            return self.pip_name

        return self.pip_name + '[' + ','.join(self.pip_options) + ']'

    def import_name(self):
        return self.import_name

    def is_installed(self):
        if self.pip_name in sys.modules:
            return True

        if importlib.util.find_spec(self.pip_name) is not None:
            return True

        try:
            __import__(self.import_name)
            return True
        except ImportError:
            pass

        return False

# List of packages to install (pip name, package name)
required = [
    # Captive Portal building
    Package('fonttools', ['woff', 'unicode'], 'fontTools'),
    Package('brotli', [], 'brotli'),
    Package('gitpython', [], 'git'),
    # esptool (while using custom version)
    Package('intelhex', [], "IntelHex"),
]

# Get all packages that are not installed
to_install = [ p.pip_package() for p in required if not p.is_installed() ]

# Install all packages that are not installed
if len(to_install) > 0:
    print('Installing: ' + ', '.join(to_install))

    # Get the python executable path
    python_path = sys.executable

    # Install the packages
    subprocess.check_call([python_path, '-m', 'pip', 'install', *to_install])
