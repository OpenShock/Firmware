---
kind: chore
---
Deploy firmware through the repository server only, and drop CI's direct CDN access

## Release Note
CI no longer uploads firmware to the CDN or advances the channel pointer files itself. The whole deployment is now three calls to the repository server — start a release, upload every board's artifacts, publish — and the run fails on the first one that does not succeed. Where artifacts are stored, which hostname serves them and the moment a channel starts offering a version are decisions the server makes behind one API call, so CI no longer holds a storage credential and can no longer leave objects, pointer files and the release index disagreeing about what is live.

The `cdn-deploy` and `cdn-bump` jobs are gone, along with the `cdn-upload-firmware`, `cdn-upload-version-info`, `cdn-bump-version` and `sftp-mirror` composite actions and the `cdn` environment they read their Bunny credentials from. Publishing is now driven by the deploy gate instead of the `OPENSHOCK_REPO_SERVER_PUBLISH` variable, which is retired: a release tag or a nightly/manual develop deploy publishes, and every other build does the same init-upload-discard dry run as before. The staged-draft-release check that guarded the CDN upload now guards the publish. The GitHub release and the API announcement still run last, in that order, each chained on the publish succeeding.
