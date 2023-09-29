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
def file_transform(filein, fileout, transform_func):
  # Create the directory if it doesn't exist.
  output_dir = os.path.dirname(fileout)
  if not os.path.exists(output_dir):
    os.makedirs(output_dir)

  try:
    with open(filein, 'r') as f:
      s = f.read()
    s = transform_func(s)
    with open(fileout, 'w') as f:
      f.write(s)
  except:
    try:
      with open(filein, 'r', encoding='utf-8') as f:
        s = f.read()
      s = transform_func(s)
      with open(fileout, 'w', encoding='utf-8') as f:
        f.write(s)
    except Exception as e:
      print('Error reading from ' + filein + ' or writing to ' + fileout + ': ' + str(e))

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
def minify_fa_css(css_path, unused_css_selectors):
  def replace_func(s):
    # Use regex to remove all the unused icons.
    for selector in unused_css_selectors:
      regex = re.escape(selector) + r'{content:"\\[a-f0-9]+";?}'
      s = re.sub(regex, '', s)
    return s

  file_transform(css_path, css_path, replace_func)
def minify_fa_font(font_path, icon_map):
  values = []
  for icon in icon_map:
    values.append(icon_map[icon]['unicode'])
  fa_unicode_csv = ','.join(values)

  tmp_path = font_path + '.tmp'

  # Use pyftsubset to remove all the unused icons.
  # pyftsubset does not support reading from and writing to the same file, so we need to write to a temporary file.
  # Then delete the original file and rename the temporary file to the original file.
  os.system('pyftsubset {} --unicodes={} --output-file={}'.format(font_path, fa_unicode_csv, tmp_path))
  file_delete(font_path)
  os.rename(tmp_path, font_path)

def build_frontend(source, target, env):
  # Change working directory to frontend.
  os.chdir('WebUI')

  # Build the captive portal only if it wasn't already built.
  # This is to avoid rebuilding the captive portal every time the firmware is built.
  # This could also lead to some annoying behaviour where the captive portal is not updated when the firmware is built.
  if not os.path.exists('build'):
    print('Building frontend...')
    os.system('npm i')
    os.system('npm run build')
    print('Frontend build complete.')

  # Change working directory back to root.
  os.chdir('..')

  fa_css = 'WebUI/build/fa/fa-all.css'
  fa_woff2 = 'WebUI/build/fa/fa-solid-900.woff2'

  # Analyze the frontend to find all the font awesome icons in use and which css selectors from fa-all.css are unused.
  (icon_map, unused_css_selectors) = get_fa_icon_map('WebUI/src', fa_css)
  print('Found ' + str(len(icon_map)) + ' font awesome icons.')

  # Write a minified css and font file to the static directory.
  minify_fa_css(fa_css, unused_css_selectors)
  minify_fa_font(fa_woff2, icon_map)

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
    def replace_func(s):
      for rename in renamed_filenames:
        s = s.replace(rename[0], rename[1])
      return s
    file_transform(action[0], action[1], replace_func)

def cleanup_frontend(source, target, env):
  print('Cleaning up frontend..')
  shutil.rmtree('WebUI/build')
  print('Front-end cleaned up.')

env.AddPreAction('$BUILD_DIR/littlefs.bin', build_frontend)
env.AddPostAction('$BUILD_DIR/littlefs.bin', cleanup_frontend)
