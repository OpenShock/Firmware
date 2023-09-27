import os
import re
import shutil

Import('env')

def exec_replace(file, old, new):
  try:
    with open(file, 'r') as f:
        s = f.read()
        s = s.replace(old, new)
    with open(file, 'w') as f:
        f.write(s)
  except:
    try:
      with open(file, 'rb') as f:
          s = f.read()
          s = s.replace(old.encode('utf-8'), new.encode('utf-8'))
      with open(file, 'wb') as f:
          f.write(s)
    except:
      print('Error replacing ' + old + ' with ' + new + ' in ' + file)

"""
Replaces all references to a path in a directory with another path.
"""
def replace_all_references(directory, oldFile, newFile):
  # Replace all references to the old path with the new path.
  for root, dirs, files in os.walk(directory):
    for filename in files:
      # Skip binary files.
      ext = filename.split('.')[-1]
      if ext == 'png' or ext == 'jpg' or ext == 'jpeg' or ext == 'gif' or ext == 'ico' or ext == 'svg' or ext == 'ttf' or ext == 'woff' or ext == 'woff2' or ext == 'eot':
        continue

      # Get the full path of the file.
      filePath = os.path.join(root, filename)

      # Replace all references to the old path with the new path.
      exec_replace(filePath, oldFile, newFile)

"""
Shorten all the filenames in the data/www/_app/immutable directory.
This is done to make LittleFS happy.
"""
def shorten_immutable_filenames(realroot, directory):
  directory = directory.replace('\\', '/')
  fileIndex = 0
  for root, dirs, files in os.walk(directory):
      for filename in files:
          newFileName = str(fileIndex) + '.' + filename.split('.')[-1]

          filePath = os.path.join(root, filename)
          newFilePath = os.path.join(root, newFileName)

          print('Renaming ' + filePath + ' to ' + newFilePath)

          # Rename the file to a shorter name.
          os.rename(filePath, newFilePath)

          # Replace all references to the old path with the new path.
          replace_all_references(realroot, filename, newFileName)

          # Increment the file index.
          fileIndex += 1

def build_frontend(source, target, env):
    # Change working directory to frontend.
    os.chdir('WebUI')

    print('Building frontend...')
    os.system('npm i')
    os.system('npm run build')
    print('Frontend build complete.')

    # Change working directory back to root.
    os.chdir('..')

    # Delete the data/www directory if it exists.
    if os.path.exists('data/www'):
        shutil.rmtree('data/www')

    # Copy the frontend build to the data/www directory.
    shutil.copytree('WebUI/build', 'data/www')

    # Shorten all the filenames in the data/www/_app/immutable directory.
    shorten_immutable_filenames('data/www', 'data/www/_app/immutable')

env.AddPreAction('$BUILD_DIR/littlefs.bin', build_frontend)
