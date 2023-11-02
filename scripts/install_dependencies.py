import sys
import subprocess
import pkg_resources
import site

# Get the path to the python executable
python = sys.executable

# Get the site-packages path(s)
package_path = site.getsitepackages()

# Get the list of installed packages
installed = {pkg.key for pkg in pkg_resources.working_set}

# Check if pip is installed
if 'pip' not in installed:
    raise RuntimeError('pip is not installed on this system')

required = ['fonttools[woff,unicode]', 'brotli']

for package in required:
    if package not in installed:
        subprocess.check_call([python, '-m', 'pip', 'install', package])
        print('Installed', package)
