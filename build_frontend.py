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
def file_delete(file):
  # Delete the file if it exists.
  if os.path.exists(file):
    os.remove(file)

def get_fa_icon_list(file):
  ext = file.split('.')[-1]
  if ext != 'svelte' and ext != 'html' and ext != 'js' and ext != 'ts':
    return []

  # Read the file.
  try:
    with open(file, 'r') as f:
      s = f.read()
  except:
    try:
      with open(file, 'rb') as f:
        s = f.read().decode('utf-8')
    except:
      print('Error reading ' + file)
      return []

  # Find all the fa icons.
  return re.findall(r'"fa (fa-[a-z0-9-]+)', s)

def get_fa_unicode_map(file):
  ext = file.split('.')[-1]
  if ext != 'css':
    return []

  # Read the file.
  try:
    with open(file, 'r') as f:
      s = f.read()
  except:
    try:
      with open(file, 'rb') as f:
        s = f.read().decode('utf-8')
    except:
      print('Error reading ' + file)
      return []

  # Find all the fa icon classes.
  css_classes = re.findall(r'((?:.fa-[a-z0-9-]+:before,?)+){content:"(\\[a-f0-9]+)";?}', s)

  # For each class, extract the fa-names.
  fa_unicode_map = {}
  for css_class in css_classes:
    fa_names = re.findall(r'.(fa-[a-z0-9-]+):before', css_class[0])
    for fa_name in fa_names:
      fa_unicode_map[fa_name] = css_class[1]

  return fa_unicode_map

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
  fa_icon_set = set()
  for root, dirs, files in os.walk('WebUI/src'):
    root = root.replace('\\', '/')
    for filename in files:
      filepath = os.path.join(root, filename)
      fa_icon_set.update(get_fa_icon_list(filepath))

  fa_unicode_map = get_fa_unicode_map('WebUI/unproc/fa-all.css')

  fa_unicode_list = []
  for fa_icon in fa_icon_set:
    if fa_icon in fa_unicode_map:
      fa_unicode_list.append(fa_unicode_map[fa_icon])
    else:
      print('Error: ' + fa_icon + ' not found in fa-all.css')

  # Convert the unicode list to a csv.
  fa_unicode_csv = ','.join(fa_unicode_list)

  file_delete('WebUI/static/fa/fa-all.css')
  file_copy('WebUI/unproc/fa-all.css', 'WebUI/static/fa/fa-all.css')

  file_delete('WebUI/static/fa/fa-solid-900.woff2')
  os.system('pyftsubset WebUI/unproc/fa-solid-900.woff2 --unicodes={} --output-file=WebUI/static/fa/fa-solid-900.woff2'.format(fa_unicode_csv))

  # Change working directory to frontend.
  os.chdir('WebUI')

  print('Building frontend...')
  os.system('npm i')
  os.system('npm run build')
  print('Frontend build complete.')

  # Change working directory back to root.
  os.chdir('..')

  # Replace with unprocessed files.
  file_delete('WebUI/static/fa/fa-all.css')
  file_copy('WebUI/unproc/fa-all.css', 'WebUI/static/fa/fa-all.css')

  file_delete('WebUI/static/fa/fa-solid-900.woff2')
  file_copy('WebUI/unproc/fa-solid-900.woff2', 'WebUI/static/fa/fa-solid-900.woff2')

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
