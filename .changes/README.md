# Unreleased Changes

Drop one markdown file per change in this directory. At release time, `scripts/release.py` folds these into `CHANGELOG.md` **and** produces a versioned `release.json` document that the OpenShock backend ingests for the website/app release feed.

**The JSON is the contract.** `CHANGELOG.md` is one renderer of it. When in doubt about the shape of a field, look at the schema below and at `scripts/release.py`.

## File format

```yaml
---
type: minor                          # required: major | minor | patch
breaking: false                      # optional: bool, defaults true if type==major
categories: [captive-portal, wifi]   # optional: list, validated against enum
# pr: 1234                           # MAINTAINER-ONLY; do not set in PRs (CI rejects it)
---

Title line for the changelog entry

Optional body with more detail, bullet points, etc.
All of this goes into CHANGELOG.md and the JSON `body` field.

- Detail one
- Detail two

## Summary

Short user-facing markdown for the website UI. Less technical than the body.

## Notices

- warning: Users must re-pair their shockers after updating
- info: The captive portal now uses REST instead of WebSocket
- error: Third-party WS tools will break
```

### Fields

**type** (required): `major`, `minor`, or `patch`. Drives semver bumping.

**breaking** (optional, bool): mark a change as breaking even when it's a `minor`/`patch` bump (e.g. opt-in feature flip, config schema change). Defaults to `true` when `type: major`.

**categories** (optional, list): tags for filtering/grouping on the website. Validated against the enum below — unknown values fail the release. Empty list is fine.

Valid categories: `captive-portal`, `wifi`, `rf`, `ota`, `config`, `serial`, `security`, `frontend`, `gateway`, `gpio`, `estop`, `performance`, `build`.

**Changelog entry** (required): everything between the frontmatter and the first recognized `##` section. First line is the title, rest is the body. Only `## Summary` and `## Notices` are recognized as section breaks — any other `##` header stays as body content.

**Summary** (optional): short user-friendly markdown for the website/app UI.

**Notices** (optional): list of `level: message` pairs, one per line, prefixed with `- `. Valid levels: `info`, `warning`, `error`. Unknown levels fail the release. Notices stay attached to their parent change in the JSON output.

**pr** (maintainer-only): positive integer, the PR number this change belongs to. **Do not set this in a PR** — `check-changes` rejects PRs that introduce a `pr:` field, because the workflow auto-derives the PR number from git history at release time. It exists only as an escape hatch for maintainers to patch up files whose history doesn't resolve (migrated entries from the old `.changeset/` system, direct-push commits, etc.) by committing a fix directly to `develop`.

### Stable ID

The change's stable ID is derived from the filename: `.changes/captive-portal-wizard.md` → `id: "captive-portal-wizard"`. This ID survives title edits, so renaming a file is the only thing that creates a "new" change downstream. Choose a slug you're willing to keep.

### Minimal example

```yaml
---
type: patch
---

Fix crash on knockoff boards after network connects
```

### Release-level headline

To add a one-paragraph framing at the top of the release ("This release focuses on…"), create `.changes/_headline.md` with plain markdown (no frontmatter). It's consumed and deleted at stable-release time alongside the change files. Optional.

## Release JSON contract

`release.py` writes `release.json` to the repo root and the workflow POSTs it to the OpenShock API and attaches it to the GitHub Release.

```jsonc
{
  "schema_version": 1,             // contract version; bumped on breaking changes
  "component": "firmware",
  "version": "1.6.0",
  "tag": "1.6.0",                  // includes "-rc.N" for RCs
  "prerelease": false,
  "previous_version": "1.5.0",     // last stable, even for RCs
  "released_at": "2026-05-26T14:23:00Z",
  "commit": "30663e6...",
  "headline": { "format": "markdown", "text": "..." },   // or null
  "changes": [
    {
      "id": "captive-portal-wizard",
      "type": "minor",
      "breaking": false,
      "categories": ["captive-portal", "frontend"],
      "pr": 1234,                                                  // optional, auto-derived
      "title":   { "format": "markdown", "text": "..." },
      "body":    { "format": "markdown", "text": "..." },          // optional
      "summary": { "format": "markdown", "text": "..." },          // optional
      "notices": [ { "level": "info", "message": "..." } ]
    }
  ]
}
```

Every human-readable text field is `{format, text}` so adding `format: "html"` or `format: "plain"` later is non-breaking. `pr` is auto-derived via `gh api` (best-effort; omitted on failure). Notices nest inside their parent change — a consumer can `flatMap` for a banner UI, but the parent link cannot be reconstructed from a flat list.

## Release workflow

```bash
python scripts/release.py status        # See pending changes and next version
python scripts/release.py rc            # Create or bump an RC tag (writes release.json)
python scripts/release.py stable        # Promote to stable, consume changes (writes release.json)
python scripts/release.py --dry-run rc  # Preview without making changes (prints JSON to stdout)
```

Branch model: PRs land in `develop`; merges to `beta` cut RC tags; merges to `master` cut stable releases. The `release.yml` workflow handles both automatically. `check-changes.yml` runs schema validation on every PR so malformed change files fail at PR time, not at release time.

Install script dependencies locally with `pip install -r scripts/requirements.txt`.
