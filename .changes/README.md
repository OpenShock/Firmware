# Change Files

Each `.md` file in this directory describes one pending change that will be
included in the next release.

## Format

```
---
kind: fixed       # safety | security | added | changed | deprecated | removed | fixed | chore
breaking: false   # optional; true forces a major semver bump
mandatory: false  # optional; true means this version must be installed before newer ones
# Optional extras:
# upgrade:                 # what users must know before installing
#   min_version: 1.4.0     #   oldest version that can update directly
#   usb_only: true         #   cannot be installed over the air
#   config_reset: true     #   the update resets the configuration
#   no_downgrade: true     #   earlier versions cannot be installed afterwards
# advisories: [CVE-2026-1234]  # security only: CVE or GHSA ids
# authors: [someone]           # GitHub logins to credit besides the commit author
# yanks: [1.2.3]               # earlier versions this release withdraws
---
Fix the crash when pairing a shocker (one line, imperative mood)

## Release Note
User-facing title line (release.json)
What changed for the user, in plain words. Shown in the GitHub Release.
- List items are implementation detail; they are folded under "Details".

## Notices
- caution: risk of harm or bricking, read before installing
- warning: behaviour that changes under the user
- important: something the user has to do
- note: good to know
```

Notices are shown as alerts at the top of the GitHub Release. Write each as
the change in bold, then the consequence, then what to do.

A breaking change must have a `## Release Note` or a notice that tells users
what to do; `check` and releases fail otherwise.

## Semver derivation

- `breaking: true` -> major bump
- `kind: fixed|security|safety|chore` -> patch bump
- everything else -> minor bump

## File naming

Name the file after the change (e.g. `add-user-auth.md`).
Run `release-tool new "<title>" --kind added` to generate one automatically.

## Special files

Files starting with `_` are not change files.

- `_headline.md` - what matters most in the release, shown at the top of the
  GitHub Release body. Consumed by the stable release.
- `_known-issues.md` - one `- ` item per open issue, with its workaround. Shown
  in every release until removed.

A stable release moves the change files it consumed to `released/<version>/`.
