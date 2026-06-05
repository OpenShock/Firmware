---
kind: chore
---

Replace hand-rolled release.py with release-tool action

Removes `scripts/release.py` and its PyYAML dependency. Release orchestration (version bumping, changelog generation, release.json production) is now handled by the `OpenShock/release-tool` GitHub Action. PR change-file checking is split into a separate `pr-check-comment.yml` workflow triggered via `workflow_run`.
