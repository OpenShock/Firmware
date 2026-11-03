---
kind: chore
---
Release as commit, tag and draft, then start the release build by dispatch

## Release Note
Releases now publish the release commit, then the tag, then stage the draft release against that tag, and only then start the release build, by dispatching ci-build on the tag. Pushing a tag no longer starts ci-build, so the build can never run ahead of the draft it uploads to, and a rejected commit never leaves an immutable tag behind.

- ci-build's gate sends a release dispatch to prod and refuses one that is not on a tag; only that dispatch runs the release job, and a manual dispatch on a tag republishes without publishing the release
- release.yml takes an optional version, so a master release can use a given version instead of the computed one (e.g. to skip one an immutable release used up); release-tool refuses it on beta
