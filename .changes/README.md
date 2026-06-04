# Unreleased Changes

Drop one markdown file per change in this directory. At release time, `release-tool` folds these into `CHANGELOG.md` and produces a versioned `release.json` document that the OpenShock backend ingests.

## File format

```yaml
---
type: minor                          # required: major | minor | patch
breaking: false                      # optional: bool, defaults true if type==major
categories: [captive-portal, wifi]   # optional: list, validated against enum
pr: 123                              # optional: see below
---

Title line for the changelog entry

Optional body with more detail, bullet points, etc.

- Detail one
- Detail two

## Release Note

Short user-facing markdown for the website/app UI.

## Notices

- warning: Users must re-pair their shockers after updating
- info: The captive portal now uses REST instead of WebSocket
- error: Third-party WS tools will break
```

### Fields

**type** (required): `major`, `minor`, or `patch`. Drives semver bumping.

**breaking** (optional, bool): mark a change as breaking even when it's a `minor`/`patch` bump. Defaults to `true` when `type: major`.

**categories** (optional, list): tags for filtering/grouping. Validated against the allowlist in `config.json` — unknown values fail validation.

Valid categories: `captive-portal`, `wifi`, `rf`, `ota`, `config`, `serial`, `security`, `frontend`, `gateway`, `gpio`, `estop`, `performance`, `build`.

**pr** (optional, tri-state):
- absent: PR number is derived from git history
- integer (`pr: 123`): used verbatim
- `pr: null`: suppresses the PR link entirely

**Changelog entry** (required): everything between the frontmatter and the first `##` section. First line is the title, rest is the body.

**Release Note** (optional): short user-friendly markdown for the website/app UI. Included in `release.json`, not in `CHANGELOG.md`.

**Notices** (optional): list of `level: message` pairs. Valid levels: `info`, `warning`, `error`.

### Stable ID

The change's stable ID is derived from the filename: `.changes/captive-portal-wizard.md` → `id: "captive-portal-wizard"`.

### Minimal example

```yaml
---
type: patch
---

Fix crash on knockoff boards after network connects
```

### Release-level headline

Create `.changes/_headline.md` with plain markdown (no frontmatter) to add a framing paragraph at the top of the release. Consumed at stable-release time.

## Release workflow

```bash
release-tool status                    # see pending changes and next version
release-tool prerelease                # create/bump a prerelease tag (writes release.json)
release-tool release                   # promote to stable, consume changes (writes release.json)
release-tool prerelease --dry-run      # preview without making changes
```

Branch model: PRs land in `develop`; merges to `beta` cut prerelease tags; merges to `master` cut stable releases. `release.yml` handles all of this automatically via the branch config in `config.json`. `check-changes.yml` validates change files on every PR so malformed files fail at PR time, not at release time.
