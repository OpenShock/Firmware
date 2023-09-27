import os
import re
import shutil

Import('env')

def file_copy(oldFile, newFile):
  # Create the directory if it doesn't exist.
  output_dir = os.path.dirname(newFile)
  if not os.path.exists(output_dir):
    os.makedirs(output_dir)

  # Copy the file.
  shutil.copyfile(oldFile, newFile)

def exec_replace(filein, fileout, replace_array):
  # Create the directory if it doesn't exist.
  output_dir = os.path.dirname(fileout)
  if not os.path.exists(output_dir):
    os.makedirs(output_dir)

  try:
    with open(filein, 'r') as f:
      s = f.read()
    for action in replace_array:
      s = s.replace(action[0], action[1])
    with open(fileout, 'w') as f:
      f.write(s)
  except:
    try:
      with open(filein, 'rb') as f:
        s = f.read()
      for action in replace_array:
        s = s.replace(action[0].encode('utf-8'), action[1].encode('utf-8'))
      with open(fileout, 'wb') as f:
        f.write(s)
    except:
      print('Error replacing ' + old + ' with ' + new + ' in ' + filein)

def build_frontend(source, target, env):
    # Change working directory to frontend.
    os.chdir('WebUI')

    print('Building frontend...')
    os.system('npm i')
    os.system('npm run build')
    print('Frontend build complete.')

    # Change working directory back to root.
    os.chdir('..')

    # Shorten all the filenames in the data/www/_app/immutable directory.
    fileIndex = 0
    copy_actions = []
    rename_actions = []
    renamed_filenames = []
    for root, dirs, files in os.walk('WebUI/build'):
      root = root.replace('\\', '/')
      newroot = root.replace('WebUI/build', 'data/www')
      isImmutable = '_app/immutable' in root

      for filename in files:
        filepath = os.path.join(root, filename)

        if isImmutable:
          newfilename = str(fileIndex) + '.' + filename.split('.')[-1]
          renamed_filenames.append((filename, newfilename))
          newfilepath = os.path.join(newroot, newfilename)
          fileIndex += 1
        else:
          newfilepath = os.path.join(newroot, filename)

        # Skip formatting binary files.
        ext = filename.split('.')[-1]
        if ext == 'png' or ext == 'jpg' or ext == 'jpeg' or ext == 'gif' or ext == 'ico' or ext == 'svg' or ext == 'ttf' or ext == 'woff' or ext == 'woff2' or ext == 'eot':
          copy_actions.append((filepath, newfilepath))
        else:
          rename_actions.append((filepath, newfilepath))

    # Delete the data/www directory if it exists.
    if os.path.exists('data/www'):
        shutil.rmtree('data/www')

    for action in copy_actions:
      file_copy(action[0], action[1])

    for action in rename_actions:
      exec_replace(action[0], action[1], renamed_filenames)

env.AddPreAction('$BUILD_DIR/littlefs.bin', build_frontend)
