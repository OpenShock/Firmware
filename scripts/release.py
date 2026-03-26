#!/usr/bin/env python3
"""
Release helper for OpenShock firmware.

Reads .changes/*.md files, determines the semver bump, and manages
version tagging, CHANGELOG.md generation, and release.json export.

Usage:
  python scripts/release.py status       Show pending changes and next version
  python scripts/release.py rc           Create or bump an RC tag
  python scripts/release.py stable       Promote to stable, consume changes into CHANGELOG.md
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

CHANGES_DIR = '.changes'
CHANGELOG_FILE = 'CHANGELOG.md'
RELEASE_JSON_FILE = 'release.json'
BUMP_ORDER = {'patch': 0, 'minor': 1, 'major': 2}
NOTICE_LEVELS = {'info', 'warning', 'error'}


@dataclass
class Notice:
    level: str
    message: str


@dataclass
class Change:
    bump: str
    title: str
    body: str
    summary: str
    notices: list
    filename: str


def get_project_root():
    d = Path(__file__).resolve().parent.parent
    if (d / 'platformio.ini').exists():
        return d
    return Path.cwd()


def run_git(*args):
    result = subprocess.run(['git'] + list(args), capture_output=True, text=True, cwd=get_project_root())
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def get_latest_stable_tag():
    tags = run_git('tag', '--sort=-v:refname')
    if not tags:
        return None
    for tag in tags.splitlines():
        tag = tag.strip()
        if re.match(r'^\d+\.\d+\.\d+$', tag):
            return tag
    return None


def get_latest_rc_tag(base_version):
    tags = run_git('tag', '--sort=-v:refname')
    if not tags:
        return None
    pattern = re.compile(rf'^{re.escape(base_version)}-rc\.(\d+)$')
    best = 0
    for tag in tags.splitlines():
        m = pattern.match(tag.strip())
        if m:
            best = max(best, int(m.group(1)))
    return best if best > 0 else None


def parse_version(version_str):
    m = re.match(r'^(\d+)\.(\d+)\.(\d+)', version_str)
    if not m:
        raise ValueError(f'Invalid version: {version_str}')
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def bump_version(major, minor, patch, bump):
    if bump == 'major':
        return major + 1, 0, 0
    elif bump == 'minor':
        return major, minor + 1, 0
    else:
        return major, minor, patch + 1


def parse_notices(text):
    """Parse a notices section into a list of Notice objects."""
    notices = []
    for line in text.strip().split('\n'):
        line = line.strip()
        if not line or not line.startswith('- '):
            continue
        line = line[2:]  # strip "- "
        m = re.match(r'^(info|warning|error):\s*(.+)$', line)
        if m:
            notices.append(Notice(level=m.group(1), message=m.group(2).strip()))
    return notices


def parse_change_file(path):
    """Parse a change file with YAML frontmatter and optional ## sections."""
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    filename = os.path.basename(path)

    # Parse YAML frontmatter
    m = re.match(r'^---\s*\n(.*?)\n---\s*\n(.*)$', content, re.DOTALL)
    if not m:
        print(f'Warning: skipping {filename} (no YAML frontmatter)', file=sys.stderr)
        return None

    frontmatter = m.group(1)
    body_raw = m.group(2).strip()

    # Extract type
    type_match = re.search(r'^type:\s*(major|minor|patch)\s*$', frontmatter, re.MULTILINE)
    if not type_match:
        print(f'Warning: skipping {filename} (missing or invalid "type")', file=sys.stderr)
        return None

    bump = type_match.group(1)

    # Split into sections by ## headers
    sections = re.split(r'^(## \w+)\s*$', body_raw, flags=re.MULTILINE)

    # First chunk is the changelog (title + body)
    changelog_raw = sections[0].strip()

    # Parse remaining sections
    summary = ''
    notices = []
    i = 1
    while i < len(sections) - 1:
        header = sections[i].strip()
        content_block = sections[i + 1].strip()
        if header == '## Summary':
            summary = content_block
        elif header == '## Notices':
            notices = parse_notices(content_block)
        i += 2

    # Split changelog into title (first line) and body (rest)
    changelog_lines = changelog_raw.split('\n')
    title = changelog_lines[0].strip()
    body = '\n'.join(changelog_lines[1:]).strip()

    return Change(
        bump=bump,
        title=title,
        body=body,
        summary=summary,
        notices=notices,
        filename=filename,
    )


def read_changes():
    root = get_project_root()
    changes = []
    for path in sorted(glob.glob(str(root / CHANGES_DIR / '*.md'))):
        if os.path.basename(path).lower() == 'readme.md':
            continue
        change = parse_change_file(path)
        if change:
            changes.append(change)
    return changes


def get_highest_bump(changes):
    if not changes:
        return None
    return max((c.bump for c in changes), key=lambda b: BUMP_ORDER.get(b, 0))


def cmd_status(args):
    changes = read_changes()
    if not changes:
        print('No pending changes.')
        return

    latest = get_latest_stable_tag()
    print(f'Latest stable tag: {latest or "(none)"}')

    highest = get_highest_bump(changes)
    if latest:
        major, minor, patch = parse_version(latest)
        new_major, new_minor, new_patch = bump_version(major, minor, patch, highest)
        next_ver = f'{new_major}.{new_minor}.{new_patch}'
        print(f'Bump level: {highest}')
        print(f'Next version: {next_ver}')
    print()

    for c in changes:
        flags = []
        if c.summary:
            flags.append('summary')
        if c.notices:
            flags.append(f'{len(c.notices)} notices')
        extra = f'  ({", ".join(flags)})' if flags else ''
        print(f'  [{c.bump}] {c.title}{extra}  <- {c.filename}')


def cmd_rc(args):
    changes = read_changes()
    if not changes:
        print('No pending changes, nothing to release.')
        return 1

    latest = get_latest_stable_tag()
    if not latest:
        print('Error: no stable tag found to base RC on.', file=sys.stderr)
        return 1

    highest = get_highest_bump(changes)
    major, minor, patch = parse_version(latest)
    new_major, new_minor, new_patch = bump_version(major, minor, patch, highest)
    base = f'{new_major}.{new_minor}.{new_patch}'

    existing_rc = get_latest_rc_tag(base)
    rc_num = (existing_rc or 0) + 1
    tag = f'{base}-rc.{rc_num}'

    if args.dry_run:
        print(f'Would create tag: {tag}')
        return

    run_git('tag', tag)
    print(f'Created tag: {tag}')
    print(f'Push with: git push origin {tag}')


def build_changelog_entry(tag, latest, changes):
    """Build a CHANGELOG.md entry from changes."""
    lines = [f'# Version {tag} Release Notes\n']

    for level in ['major', 'minor', 'patch']:
        level_changes = [c for c in changes if c.bump == level]
        if not level_changes:
            continue
        for c in level_changes:
            lines.append(f'- {c.title}')
            if c.body:
                for bline in c.body.split('\n'):
                    lines.append(f'  {bline}' if bline.strip() else '')
        lines.append('')

    # Collect all notices
    all_notices = []
    for c in changes:
        all_notices.extend(c.notices)

    if all_notices:
        lines.append('### Notices\n')
        for n in all_notices:
            lines.append(f'- **{n.level.upper()}**: {n.message}')
        lines.append('')

    lines.append(f'**Full Changelog: [{latest} -> {tag}](https://github.com/OpenShock/Firmware/compare/{latest}...{tag})**\n')
    return '\n'.join(lines)


def build_release_json(tag, latest, changes):
    """Build a release.json for the website."""
    data = {
        'version': tag,
        'previous_version': latest,
        'date': datetime.now().strftime('%Y-%m-%d'),
        'changes': [],
        'notices': [],
    }

    for c in changes:
        entry = {
            'type': c.bump,
            'title': c.title,
        }
        if c.body:
            entry['body'] = c.body
        if c.summary:
            entry['summary'] = c.summary
        data['changes'].append(entry)

    for c in changes:
        for n in c.notices:
            data['notices'].append({
                'level': n.level,
                'message': n.message,
            })

    return data


def cmd_stable(args):
    changes = read_changes()
    if not changes:
        print('No pending changes, nothing to release.')
        return 1

    latest = get_latest_stable_tag()
    if not latest:
        print('Error: no stable tag found.', file=sys.stderr)
        return 1

    highest = get_highest_bump(changes)
    major, minor, patch = parse_version(latest)
    new_major, new_minor, new_patch = bump_version(major, minor, patch, highest)
    tag = f'{new_major}.{new_minor}.{new_patch}'

    entry = build_changelog_entry(tag, latest, changes)
    release_data = build_release_json(tag, latest, changes)

    if args.dry_run:
        print(f'Would create tag: {tag}')
        print(f'\nChangelog entry:\n{entry}')
        print(f'\nrelease.json:\n{json.dumps(release_data, indent=2)}')
        return

    root = get_project_root()

    # Write release.json
    release_json_path = root / RELEASE_JSON_FILE
    with open(release_json_path, 'w', encoding='utf-8') as f:
        json.dump(release_data, f, indent=2)
        f.write('\n')
    print(f'Wrote {RELEASE_JSON_FILE}')

    # Prepend to CHANGELOG.md
    changelog_path = root / CHANGELOG_FILE
    existing = ''
    if changelog_path.exists():
        existing = changelog_path.read_text(encoding='utf-8')
    changelog_path.write_text(entry + '\n' + existing, encoding='utf-8')
    print(f'Updated {CHANGELOG_FILE}')

    # Delete change files
    for c in changes:
        os.remove(root / CHANGES_DIR / c.filename)
    print(f'Removed {len(changes)} change files')

    # Commit and tag
    run_git('add', CHANGELOG_FILE, RELEASE_JSON_FILE, CHANGES_DIR)
    run_git('commit', '-m', f'chore: release {tag}')
    run_git('tag', tag)
    print(f'Created tag: {tag}')
    print(f'Push with: git push origin {tag} && git push')


def main():
    parser = argparse.ArgumentParser(description='OpenShock firmware release helper')
    parser.add_argument('--dry-run', action='store_true', help='Show what would happen without making changes')
    sub = parser.add_subparsers(dest='command')

    sub.add_parser('status', help='Show pending changes and next version')
    sub.add_parser('rc', help='Create or bump an RC tag')
    sub.add_parser('stable', help='Promote to stable release')

    args = parser.parse_args()

    if args.command == 'status' or args.command is None:
        return cmd_status(args)
    elif args.command == 'rc':
        return cmd_rc(args)
    elif args.command == 'stable':
        return cmd_stable(args)


if __name__ == '__main__':
    sys.exit(main() or 0)
