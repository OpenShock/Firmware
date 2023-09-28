import os
import re
import shutil

Import('env')

def file_copy(oldFile, newFile):
  # Create the directory if it doesn't exist.
  output_dir = os.path.dirname(newFile)
  if not os.path.exists(output_dir):
    os.makedirs(output_dir)
  elif os.path.exists(newFile):
    os.remove(newFile)

  # Copy the file.
  shutil.copyfile(oldFile, newFile)
def file_delete(file):
  # Delete the file if it exists.
  if os.path.exists(file):
    os.remove(file)
def file_read_text(file):
  try:
    with open(file, 'r') as f:
      return f.read()
  except:
    try:
      with open(file, 'r', encoding='utf-8') as f:
        return f.read()
    except:
      print('Error reading ' + file)
      return ''

def get_fa_icon_map(srcdir, csspath):
  cssext = csspath.split('.')[-1]
  if cssext != 'css':
    return []

  fa_icon_set = set()
  for root, dirs, files in os.walk(srcdir):
    root = root.replace('\\', '/')
    for filename in files:
      filepath = os.path.join(root, filename)
      ext = filename.split('.')[-1]
      if ext != 'svelte' and ext != 'html' and ext != 'js' and ext != 'ts':
        continue

      s = file_read_text(filepath)
      if s == '':
        continue

      icons = re.findall(r'(fa-[a-z0-9-]+)', s)

      fa_icon_set.update(icons)

  # Find all the fa icon classes.
  s = file_read_text(csspath)
  css_classes = re.findall(r'((?:.fa-[a-z0-9-]+:before,?)+){content:"(\\[a-f0-9]+)";?}', s)

  # For each class, extract the fa-names.
  icon_map = {}
  unused_css_selectors = []
  for css_class in css_classes:
    fa_names = re.findall(r'.(fa-[a-z0-9-]+):before', css_class[0])

    any_in_set = False
    for fa_name in fa_names:
      if fa_name in fa_icon_set:
        icon_map[fa_name] = { 'unicode': css_class[1], 'selector': css_class[0] }
        any_in_set = True

    if not any_in_set:
      unused_css_selectors.append(css_class[0])

  return (icon_map, unused_css_selectors)
def minify_fa_css(srcpath, dstpath, unused_css_selectors):
  s = file_read_text(srcpath)

  # Use regex to remove all the unused icons.
  for selector in unused_css_selectors:
    regex = re.escape(selector) + r'{content:"\\[a-f0-9]+";?}'
    s = re.sub(regex, '', s)

  file_delete(dstpath)
  with open(dstpath, 'w') as f:
    f.write(s)
def minify_fa_font(srcpath, dstpath, icon_map):
  values = []
  for icon in icon_map:
    values.append(icon_map[icon]['unicode'])
  fa_unicode_csv = ','.join(values)

  file_delete(dstpath)
  os.system('pyftsubset {} --unicodes={} --output-file={}'.format(srcpath, fa_unicode_csv, dstpath))

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
      with open(filein, 'r', encoding='utf-8') as f:
        s = f.read()
      for action in replace_array:
        s = s.replace(action[0], action[1])
      with open(fileout, 'w', encoding='utf-8') as f:
        f.write(s)
    except:
      print('Error replacing ' + old + ' with ' + new + ' in ' + filein)

def build_frontend(source, target, env):
  unproc_fa_css = 'WebUI/unproc/fa-all.css'
  unproc_fa_woff2 = 'WebUI/unproc/fa-solid-900.woff2'
  static_fa_css = 'WebUI/static/fa/fa-all.css'
  static_fa_woff2 = 'WebUI/static/fa/fa-solid-900.woff2'

  # Analyze the frontend to find all the font awesome icons in use and which css selectors from fa-all.css are unused.
  (icon_map, unused_css_selectors) = get_fa_icon_map('WebUI/src', unproc_fa_css)
  print('Found ' + str(len(icon_map)) + ' font awesome icons.')

  # Write a minified css and font file to the static directory.
  minify_fa_css(unproc_fa_css, static_fa_css, unused_css_selectors)
  minify_fa_font(unproc_fa_woff2, static_fa_woff2, icon_map)

  # Change working directory to frontend.
  os.chdir('WebUI')

  print('Building frontend...')
  os.system('npm i')
  os.system('npm run build')
  print('Frontend build complete.')

  # Change working directory back to root.
  os.chdir('..')

  # Replace the minified css and font files with the unprocessed ones.
  file_copy(unproc_fa_css, static_fa_css)
  file_copy(unproc_fa_woff2, static_fa_woff2)

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
