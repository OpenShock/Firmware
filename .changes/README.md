# Unreleased Changes

Drop one markdown file per change in this directory. At release time, `scripts/release.py` folds these into `CHANGELOG.md` and outputs release JSON for the website.

## File format

```yaml
---
type: minor
---

Title line for the changelog entry

Optional body with more detail, bullet points, etc.
All of this goes into CHANGELOG.md.

- Detail one
- Detail two

## Summary

Short user-facing text for the website UI. Less technical.

## Notices

- warning: Users must re-pair their shockers after updating
- info: The captive portal now uses REST instead of WebSocket
- error: Third-party WS tools will break
```

### Fields

**type** (required): `major`, `minor`, or `patch`

**Changelog entry** (required): Everything between the frontmatter and the first `##` section. First line is the title, rest is the body. Both go into CHANGELOG.md.

**Summary** (optional): Short user-friendly text for the website/app UI.

**Notices** (optional): Structured list of `level: message` pairs. Valid levels: `info`, `warning`, `error`. These render as alert boxes in the website UI.

### Minimal example

```yaml
---
type: patch
---

Fix crash on knockoff boards after network connects
```

## Release workflow

```bash
python scripts/release.py status        # See pending changes and next version
python scripts/release.py rc            # Create or bump a release candidate tag
python scripts/release.py stable        # Promote to stable, consume changes
python scripts/release.py --dry-run rc  # Preview without making changes
```
