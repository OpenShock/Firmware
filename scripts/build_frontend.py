import os
import re
import gzip
import shutil
import subprocess
from utils import sysenv

Import('env')  # type: ignore


def dir_ensure(dir):
    if not os.path.exists(dir):
        os.makedirs(dir)


def file_delete(file):
    if os.path.exists(file):
        os.remove(file)


def file_gzip(file_path, gzip_path):
    dir_ensure(os.path.dirname(gzip_path))
    file_delete(gzip_path)
    with open(file_path, 'rb') as f_in, gzip.open(gzip_path, 'wb') as f_out:
        f_out.write(f_in.read())


def file_write_bin(file, data):
    try:
        with open(file, 'wb') as f:
            f.write(data)
        return True
    except:
        print('Error writing to ' + file)
        return False


def file_read_text(file):
    try:
        with open(file, 'r', encoding='utf-8') as f:
            return (f.read(), 'utf-8')
    except:
        pass
    try:
        with open(file, 'r') as f:
            return (f.read(), None)
    except:
        print('Error reading ' + file)
        return (None, None)


def file_write_text(file, text, enc):
    try:
        with open(file, 'w', encoding=enc) as f:
            f.write(text)
        return True
    except:
        print('Error writing to ' + file)
        return False


def get_interpreter_path():
    # Run where or which to find the path to python.
    if os.name == 'nt':
        python_paths = subprocess.getoutput('where python').split('\n')
    else:
        python_paths = subprocess.getoutput('which python').split('\n')

    for path in python_paths:
        if 'platformio' not in path:
            return path.strip()

    first = python_paths[0].strip()
    if first == '':
        print('Error: Could not find a python interpreter.')
        exit(1)

    print('Warning: Could not find a global python interpreter. Using ' + first + ' instead.')

    return first


def pyftsubset(font_path, fa_unicode_csv, output_path):
    # Get the path to python, optimally the global python interpreter.
    python_path = get_interpreter_path()

    # Use pyftsubset to remove all the unused icons.
    # pyftsubset does not support reading from and writing to the same file, so we need to write to a temporary file.
    # Then delete the original file and rename the temporary file to the original file.
    pyftsubset_cmd = (
        f'{python_path} -m fontTools.subset {font_path} --unicodes={fa_unicode_csv} --output-file={output_path}'
    )

    print('Running: ' + pyftsubset_cmd)

    # Run pyftsubset.
    os.system(pyftsubset_cmd)


def get_fa_icon_map(srcdir, csspath):
    ext = csspath.split('.')[-1]
    if ext != 'css':
        return []

    fa_icon_set = set()
    for root, dirs, files in os.walk(srcdir):
        root = root.replace('\\', '/')
        for filename in files:
            filepath = os.path.join(root, filename)
            ext = filename.split('.')[-1]
            if ext == 'ico' or ext == 'woff2':  # Skip binary files.
                continue

            (s, _) = file_read_text(filepath)
            if s == None or s == '':
                continue

            icons = re.findall(r'(fa-[a-z0-9-]+)', s)

            fa_icon_set.update(icons)

    # Find all the fa icon classes.
    (s, _) = file_read_text(csspath)
    if s == None or s == '':
        return []

    css_classes = re.findall(r'((?:.fa-[a-z0-9-]+:before,?)+){content:"(\\[a-f0-9]+)";?}', s)

    # For each class, extract the fa-names.
    icon_map = {}
    unused_css_selectors = []
    for css_class in css_classes:
        fa_names = re.findall(r'.(fa-[a-z0-9-]+):before', css_class[0])

        any_in_set = False
        for fa_name in fa_names:
            if fa_name in fa_icon_set:
                icon_map[fa_name] = {'unicode': css_class[1], 'selector': css_class[0]}
                any_in_set = True

        if not any_in_set:
            unused_css_selectors.append(css_class[0])

    return (icon_map, unused_css_selectors)


def minify_fa_css(css_path, unused_css_selectors):
    (s, enc) = file_read_text(css_path)
    if s == None or s == '':
        return
    # Use regex to remove all the unused icons.
    for selector in unused_css_selectors:
        regex = re.escape(selector) + r'{content:"\\[a-f0-9]+";?}'
        s = re.sub(regex, '', s)
    try:
        with open(css_path, 'w', encoding=enc) as f_out:
            f_out.write(s)
    except Exception as e:
        print('Error writing to ' + css_path + ': ' + str(e))


def minify_fa_font(font_path, icon_map):
    values = []
    for icon in icon_map:
        values.append(icon_map[icon]['unicode'])
    fa_unicode_csv = ','.join(values)

    tmp_path = font_path + '.tmp'

    # Use pyftsubset to remove all the unused icons.
    pyftsubset(font_path, fa_unicode_csv, tmp_path)

    # Delete the original font file and rename the temporary file to the original file.
    file_delete(font_path)
    os.rename(tmp_path, font_path)


def build_frontend(source, target, env):
    # Change working directory to frontend.
    os.chdir('WebUI')

    # Build the captive portal only if it wasn't already built.
    # This is to avoid rebuilding the captive portal every time the firmware is built.
    # This could also lead to some annoying behaviour where the captive portal is not updated when the firmware is built.
    if not sysenv.get_bool('CI', False):
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
            if (
                ext == 'png'
                or ext == 'jpg'
                or ext == 'jpeg'
                or ext == 'gif'
                or ext == 'ico'
                or ext == 'svg'
                or ext == 'ttf'
                or ext == 'woff'
                or ext == 'woff2'
                or ext == 'gz'
            ):
                copy_actions.append((filepath, newfilepath))
            else:
                rename_actions.append((filepath, newfilepath))

    # Delete the data/www directory if it exists.
    if os.path.exists('data/www'):
        shutil.rmtree('data/www')

    for src_path, dst_path in copy_actions:
        if src_path.endswith('.gz'):
            # Don't gzip files that are already gzipped.
            shutil.copyfile(src_path, dst_path)
        else:
            file_gzip(src_path, dst_path + '.gz')

    for src_path, dst_path in rename_actions:
        dir_ensure(os.path.dirname(dst_path))
        file_delete(dst_path)
        (s, enc) = file_read_text(src_path)
        if s == None:
            print('Error reading ' + src_path)
            continue

        for old, new in renamed_filenames:
            s = s.replace(old, new)

        try:
            if enc == 'utf-8':
                s = s.encode('utf-8')
            else:
                s = s.encode('ascii')
        except Exception as e:
            print('Error encoding ' + src_path + ': ' + str(e))
            continue

        with gzip.open(dst_path + '.gz', 'wb') as f_out:
            f_out.write(s)
        # If the file is index.html, also write a non-gzipped version.
        if src_path.endswith('index.html'):
            file_write_bin(dst_path, s)


env.AddPreAction('$BUILD_DIR/littlefs.bin', build_frontend)
