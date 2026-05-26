#!/usr/bin/env python3
"""
Release helper for OpenShock firmware.

Reads .changes/*.md files, determines the semver bump, manages version tagging
and CHANGELOG.md generation, and writes a versioned release.json document for
ingestion by the OpenShock backend.

Usage:
  python scripts/release.py status       Show pending changes and next version
  python scripts/release.py rc           Create or bump an RC tag, write release.json
  python scripts/release.py stable       Promote to stable, consume changes, write release.json

Release JSON contract (schema_version 1):

  {
    "schema_version": 1,
    "component": "firmware",
    "version": "1.6.0",
    "tag": "1.6.0",
    "prerelease": false,
    "previous_version": "1.5.0",
    "released_at": "2026-05-26T14:23:00Z",
    "commit": "30663e6...",
    "headline": { "format": "markdown", "text": "..." } | null,
    "changes": [
      {
        "id": "captive-portal-wizard",
        "type": "minor",
        "breaking": false,
        "categories": ["captive-portal", "frontend"],
        "pr": 1234,                                       // optional
        "title":   { "format": "markdown", "text": "..." },
        "body":    { "format": "markdown", "text": "..." },   // optional
        "summary": { "format": "markdown", "text": "..." },   // optional
        "notices": [ { "level": "info|warning|error", "message": "..." } ]
      }
    ]
  }

Every text field is `{format, text}` so adding new formats (html, plain) later
is non-breaking. `notices` nest inside their parent change so consumers can
group alerts by change card; a flat list is one flatMap away.

Recovery: tags are pushed by CI. If a publishing step fails after the tag was
pushed, re-running the workflow on the same ref will fail because the tag
exists. Check out the tag and run `release.py stable --dry-run` to regenerate
release.json, then re-publish manually.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import List, Optional

import yaml

CHANGES_DIR = '.changes'
CHANGELOG_FILE = 'CHANGELOG.md'
HEADLINE_FILE = '_headline.md'
README_FILE = 'README.md'
COMPONENT = 'firmware'
SCHEMA_VERSION = 1

BUMP_ORDER = {'patch': 0, 'minor': 1, 'major': 2}
VALID_TYPES = {'major', 'minor', 'patch'}
VALID_NOTICE_LEVELS = {'info', 'warning', 'error'}
VALID_CATEGORIES = {
    'captive-portal', 'wifi', 'rf', 'ota', 'config', 'serial',
    'security', 'frontend', 'gateway', 'gpio', 'estop',
    'performance', 'build',
}


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
    notices: List[Notice]
    filename: str
    breaking: bool = False
    categories: List[str] = field(default_factory=list)
    pr: Optional[int] = None

    @property
    def slug(self) -> str:
        # Strip .md extension to derive a stable id from the filename.
        return os.path.splitext(self.filename)[0]


def get_project_root() -> Path:
    d = Path(__file__).resolve().parent.parent
    if (d / 'platformio.ini').exists():
        return d
    return Path.cwd()


def run_git(*args: str) -> str:
    result = subprocess.run(['git'] + list(args), capture_output=True, text=True, cwd=get_project_root())
    if result.returncode != 0:
        cmd = 'git ' + ' '.join(args)
        details = [f'Git command failed ({result.returncode}): {cmd}']
        if result.stderr and result.stderr.strip():
            details.append(result.stderr.strip())
        elif result.stdout and result.stdout.strip():
            details.append(result.stdout.strip())
        raise SystemExit('\n'.join(details))
    return result.stdout.strip()


def get_latest_stable_tag() -> Optional[str]:
    tags = run_git('tag', '--sort=-v:refname')
    if not tags:
        return None
    for tag in tags.splitlines():
        tag = tag.strip()
        if re.match(r'^\d+\.\d+\.\d+$', tag):
            return tag
    return None


def get_latest_rc_number(base_version: str) -> Optional[int]:
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


def parse_version(version_str: str):
    m = re.match(r'^(\d+)\.(\d+)\.(\d+)', version_str)
    if not m:
        raise ValueError(f'Invalid version: {version_str}')
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def bump_version(major: int, minor: int, patch: int, bump: str):
    if bump == 'major':
        return major + 1, 0, 0
    if bump == 'minor':
        return major, minor + 1, 0
    return major, minor, patch + 1


def parse_notices(value, errors: List[str], filename: str) -> List[Notice]:
    """Parse a '## Notices' body block. Accepts lines of the form '- level: message'."""
    notices = []
    if not isinstance(value, str):
        return notices
    for line in value.splitlines():
        line = line.strip()
        if not line:
            continue
        if not line.startswith('- '):
            errors.append(f'{filename}: notices line must start with "- ": {line!r}')
            continue
        body = line[2:].strip()
        m = re.match(r'^([A-Za-z]+)\s*:\s*(.+)$', body)
        if not m:
            errors.append(f'{filename}: notice missing "level: message" form: {line!r}')
            continue
        level = m.group(1).lower()
        if level not in VALID_NOTICE_LEVELS:
            errors.append(
                f'{filename}: invalid notice level {level!r} '
                f'(valid: {sorted(VALID_NOTICE_LEVELS)})'
            )
            continue
        notices.append(Notice(level=level, message=m.group(2).strip()))
    return notices


# Allowlisted body sections. Anything else with ## is treated as body content.
_SECTION_HEADERS = ('## Summary', '## Notices')


def split_sections(body_raw: str):
    """Split body into (changelog, summary, notices_raw) using an allowlist of headers."""
    lines = body_raw.splitlines()
    sections = {'_changelog': []}
    current = '_changelog'
    for line in lines:
        stripped = line.strip()
        if stripped in _SECTION_HEADERS:
            current = stripped[3:].lower()  # 'summary' or 'notices'
            sections[current] = []
            continue
        sections.setdefault(current, []).append(line)
    return (
        '\n'.join(sections.get('_changelog', [])).strip(),
        '\n'.join(sections.get('summary', [])).strip(),
        '\n'.join(sections.get('notices', [])).strip(),
    )


def parse_change_file(path: str, errors: List[str]) -> Optional[Change]:
    """Parse a change file with YAML frontmatter and optional ## sections."""
    filename = os.path.basename(path)

    try:
        content = Path(path).read_text(encoding='utf-8')
    except OSError as e:
        errors.append(f'{filename}: cannot read file: {e}')
        return None

    m = re.match(r'^---\s*\r?\n(.*?)\r?\n---\s*\r?\n(.*)$', content, re.DOTALL)
    if not m:
        errors.append(f'{filename}: missing YAML frontmatter (must start with --- ... ---)')
        return None

    try:
        frontmatter = yaml.safe_load(m.group(1)) or {}
    except yaml.YAMLError as e:
        errors.append(f'{filename}: invalid YAML frontmatter: {e}')
        return None

    if not isinstance(frontmatter, dict):
        errors.append(f'{filename}: frontmatter must be a YAML mapping')
        return None

    # type (required)
    bump = frontmatter.get('type')
    if bump not in VALID_TYPES:
        errors.append(f'{filename}: "type" must be one of {sorted(VALID_TYPES)} (got {bump!r})')
        return None

    # breaking (optional; defaults true for major)
    breaking = frontmatter.get('breaking')
    if breaking is None:
        breaking = (bump == 'major')
    elif not isinstance(breaking, bool):
        errors.append(f'{filename}: "breaking" must be a boolean (got {breaking!r})')
        breaking = (bump == 'major')

    # categories (optional)
    categories_raw = frontmatter.get('categories', [])
    if isinstance(categories_raw, str):
        errors.append(f'{filename}: "categories" must be a list, not a string')
        categories_raw = []
    if not isinstance(categories_raw, list):
        errors.append(f'{filename}: "categories" must be a list (got {type(categories_raw).__name__})')
        categories_raw = []
    categories = []
    for c in categories_raw:
        if not isinstance(c, str):
            errors.append(f'{filename}: category entries must be strings (got {c!r})')
            continue
        if c not in VALID_CATEGORIES:
            errors.append(
                f'{filename}: unknown category {c!r} '
                f'(valid: {sorted(VALID_CATEGORIES)})'
            )
            continue
        categories.append(c)

    body_raw = m.group(2).strip()
    changelog_raw, summary, notices_raw = split_sections(body_raw)

    if not changelog_raw:
        errors.append(f'{filename}: missing title line (first non-frontmatter content)')
        return None

    changelog_lines = changelog_raw.split('\n')
    title = changelog_lines[0].strip()
    body = '\n'.join(changelog_lines[1:]).strip()

    if not title:
        errors.append(f'{filename}: title line is empty')
        return None

    notices = parse_notices(notices_raw, errors, filename) if notices_raw else []

    return Change(
        bump=bump,
        title=title,
        body=body,
        summary=summary,
        notices=notices,
        filename=filename,
        breaking=breaking,
        categories=categories,
    )


def read_changes(strict: bool = True) -> List[Change]:
    root = get_project_root()
    errors: List[str] = []
    changes: List[Change] = []
    for path in sorted(glob.glob(str(root / CHANGES_DIR / '*.md'))):
        name = os.path.basename(path).lower()
        if name == README_FILE.lower() or name == HEADLINE_FILE.lower():
            continue
        change = parse_change_file(path, errors)
        if change:
            changes.append(change)

    if errors:
        msg = 'Invalid change files:\n  - ' + '\n  - '.join(errors)
        if strict:
            raise SystemExit(msg)
        print(msg, file=sys.stderr)

    return changes


def read_headline() -> Optional[str]:
    root = get_project_root()
    path = root / CHANGES_DIR / HEADLINE_FILE
    if not path.exists():
        return None
    text = path.read_text(encoding='utf-8').strip()
    return text or None


def get_highest_bump(changes: List[Change]) -> Optional[str]:
    if not changes:
        return None
    return max((c.bump for c in changes), key=lambda b: BUMP_ORDER.get(b, 0))


def derive_pr_number(filename: str) -> Optional[int]:
    """Find the PR that introduced .changes/<filename> via `gh api`."""
    rel = f'{CHANGES_DIR}/{filename}'
    try:
        sha = run_git('log', '--diff-filter=A', '--format=%H', '-n', '1', '--', rel)
    except SystemExit:
        return None
    if not sha:
        return None
    try:
        result = subprocess.run(
            ['gh', 'api', f'repos/{{owner}}/{{repo}}/commits/{sha}/pulls'],
            capture_output=True, text=True, cwd=get_project_root(),
        )
    except FileNotFoundError:
        return None
    if result.returncode != 0:
        return None
    try:
        pulls = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None
    if not isinstance(pulls, list) or not pulls:
        return None
    pr = pulls[0]
    num = pr.get('number') if isinstance(pr, dict) else None
    return int(num) if isinstance(num, int) else None


def md(text: str) -> Optional[dict]:
    text = (text or '').strip()
    if not text:
        return None
    return {'format': 'markdown', 'text': text}


def md_required(text: str) -> dict:
    out = md(text)
    if out is None:
        raise ValueError('required markdown field is empty')
    return out


def build_release_data(
    tag: str,
    previous: Optional[str],
    changes: List[Change],
    headline: Optional[str],
    prerelease: bool,
    commit: str,
    version: str,
    released_at: str,
    enrich_pr: bool = True,
) -> dict:
    data = {
        'schema_version': SCHEMA_VERSION,
        'component': COMPONENT,
        'version': version,
        'tag': tag,
        'prerelease': prerelease,
        'previous_version': previous,
        'released_at': released_at,
        'commit': commit,
        'headline': md(headline) if headline else None,
        'changes': [],
    }

    for c in changes:
        entry = {
            'id': c.slug,
            'type': c.bump,
            'breaking': c.breaking,
            'categories': list(c.categories),
            'title': md_required(c.title),
        }
        body_md = md(c.body)
        if body_md:
            entry['body'] = body_md
        summary_md = md(c.summary)
        if summary_md:
            entry['summary'] = summary_md

        if enrich_pr:
            pr = c.pr if c.pr is not None else derive_pr_number(c.filename)
            if pr is not None:
                entry['pr'] = pr

        entry['notices'] = [
            {'level': n.level, 'message': n.message} for n in c.notices
        ]
        data['changes'].append(entry)

    return data


def render_changelog_markdown(data: dict) -> str:
    """Derive the CHANGELOG.md entry from the schema-v1 release dict."""
    tag = data['tag']
    previous = data.get('previous_version')
    lines = [f'# Version {tag} Release Notes\n']

    headline = data.get('headline')
    if headline:
        lines.append(headline['text'].strip())
        lines.append('')

    for level in ('major', 'minor', 'patch'):
        level_changes = [c for c in data['changes'] if c['type'] == level]
        if not level_changes:
            continue
        for c in level_changes:
            badges = []
            if c.get('breaking'):
                badges.append('**BREAKING**')
            if c.get('categories'):
                badges.append('[' + ', '.join(c['categories']) + ']')
            badge_str = (' ' + ' '.join(badges)) if badges else ''
            pr_str = f' (#{c["pr"]})' if c.get('pr') else ''
            lines.append(f'- {c["title"]["text"]}{badge_str}{pr_str}')
            body = c.get('body')
            if body:
                for bline in body['text'].split('\n'):
                    lines.append(f'  {bline}' if bline.strip() else '')
        lines.append('')

    all_notices = [(c, n) for c in data['changes'] for n in c.get('notices', [])]
    if all_notices:
        lines.append('### Notices\n')
        for c, n in all_notices:
            lines.append(f'- **{n["level"].upper()}**: {n["message"]}')
        lines.append('')

    if previous:
        lines.append(
            f'**Full Changelog: [{previous} -> {tag}]'
            f'(https://github.com/OpenShock/Firmware/compare/{previous}...{tag})**\n'
        )
    return '\n'.join(lines)


def now_utc_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace('+00:00', 'Z')


def current_commit() -> str:
    return run_git('rev-parse', 'HEAD')


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_status(args) -> int:
    changes = read_changes(strict=True)
    if not changes:
        print('No pending changes.')
        return 0

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
        if c.breaking:
            flags.append('breaking')
        if c.categories:
            flags.append('cat:' + ','.join(c.categories))
        if c.summary:
            flags.append('summary')
        if c.notices:
            flags.append(f'{len(c.notices)} notices')
        extra = f'  ({", ".join(flags)})' if flags else ''
        print(f'  [{c.bump}] {c.title}{extra}  <- {c.filename}')

    headline = read_headline()
    if headline:
        print()
        print(f'Headline ({len(headline)} chars) from {CHANGES_DIR}/{HEADLINE_FILE}')
    return 0


def _compute_next(changes: List[Change], latest: Optional[str]):
    if not latest:
        raise SystemExit('Error: no stable tag found to base release on.')
    highest = get_highest_bump(changes)
    major, minor, patch = parse_version(latest)
    new_major, new_minor, new_patch = bump_version(major, minor, patch, highest)
    return f'{new_major}.{new_minor}.{new_patch}'


def _write_release_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {path.name}', file=sys.stderr)


def cmd_rc(args) -> int:
    changes = read_changes(strict=True)
    if not changes:
        print('No pending changes, nothing to release.')
        return 0

    latest = get_latest_stable_tag()
    base = _compute_next(changes, latest)
    rc_num = (get_latest_rc_number(base) or 0) + 1
    tag = f'{base}-rc.{rc_num}'

    data = build_release_data(
        tag=tag,
        previous=latest,
        changes=changes,
        headline=read_headline(),
        prerelease=True,
        commit=current_commit(),
        version=base,
        released_at=now_utc_iso(),
        enrich_pr=not args.dry_run,
    )

    if args.dry_run:
        print(f'Would create tag: {tag}', file=sys.stderr)
        print(json.dumps(data, indent=2))
        return 0

    _write_release_json(Path(args.output), data)
    run_git('tag', tag)
    print(f'Created tag: {tag}', file=sys.stderr)
    return 0


def cmd_stable(args) -> int:
    changes = read_changes(strict=True)
    if not changes:
        print('No pending changes, nothing to release.')
        return 0

    latest = get_latest_stable_tag()
    tag = _compute_next(changes, latest)
    headline = read_headline()

    data = build_release_data(
        tag=tag,
        previous=latest,
        changes=changes,
        headline=headline,
        prerelease=False,
        commit=current_commit(),
        version=tag,
        released_at=now_utc_iso(),
        enrich_pr=not args.dry_run,
    )
    entry = render_changelog_markdown(data)

    if args.dry_run:
        print(f'Would create tag: {tag}', file=sys.stderr)
        print(f'\nChangelog entry:\n{entry}', file=sys.stderr)
        print(json.dumps(data, indent=2))
        return 0

    root = get_project_root()

    _write_release_json(Path(args.output), data)

    changelog_path = root / CHANGELOG_FILE
    existing = changelog_path.read_text(encoding='utf-8') if changelog_path.exists() else ''
    changelog_path.write_text(entry + '\n' + existing, encoding='utf-8')
    print(f'Updated {CHANGELOG_FILE}', file=sys.stderr)

    removed = 0
    for c in changes:
        os.remove(root / CHANGES_DIR / c.filename)
        removed += 1
    headline_path = root / CHANGES_DIR / HEADLINE_FILE
    if headline_path.exists():
        headline_path.unlink()
        print(f'Removed {CHANGES_DIR}/{HEADLINE_FILE}', file=sys.stderr)
    print(f'Removed {removed} change files', file=sys.stderr)

    run_git('add', CHANGELOG_FILE, CHANGES_DIR)
    run_git('commit', '-m', f'chore: release {tag}')
    run_git('tag', tag)
    print(f'Created tag: {tag}', file=sys.stderr)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description='OpenShock firmware release helper')
    parser.add_argument('--dry-run', action='store_true', help='Show what would happen without making changes')
    parser.add_argument('--output', default='release.json', help='Path to write release.json (default: release.json)')
    sub = parser.add_subparsers(dest='command')

    sub.add_parser('status', help='Show pending changes and next version')
    sub.add_parser('rc', help='Create or bump an RC tag')
    sub.add_parser('stable', help='Promote to stable release')

    args = parser.parse_args()

    if args.command == 'status' or args.command is None:
        return cmd_status(args)
    if args.command == 'rc':
        return cmd_rc(args)
    if args.command == 'stable':
        return cmd_stable(args)
    parser.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main() or 0)
