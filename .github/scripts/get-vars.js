import { setFailed, setOutput } from '@actions/core';
import ini from 'ini';
import child_process from 'node:child_process';
import fs from 'node:fs';
import semver from 'semver';

// Get branch name
const gitRef = process.env.GITHUB_REF;
if (gitRef === undefined) {
  setFailed('Environment variable "GITHUB_REF" not found');
  process.exit();
}

const isGitTag = gitRef.startsWith('refs/tags/');
const isGitBranch = gitRef.startsWith('refs/heads/');
const isGitPullRequest = gitRef.startsWith('refs/pull/') && gitRef.endsWith('/merge');

if (!isGitTag && !isGitBranch && !isGitPullRequest) {
  setFailed(`Git ref "${gitRef}" is not a valid branch, tag or pull request`);
  process.exit();
}

const gitCommitHash = process.env.GITHUB_SHA;
if (gitCommitHash === undefined) {
  setFailed('Environment variable "GITHUB_SHA" not found');
  process.exit();
}
const gitShortCommitHash = gitCommitHash.substring(0, 8);

const gitHeadRefName = isGitPullRequest ? process.env.GITHUB_HEAD_REF : gitRef.split('/')[2];
if (gitHeadRefName === undefined) {
  setFailed('Failed to get git head ref name');
  process.exit();
}

const gitTagsList = child_process
  .execSync('git for-each-ref --sort=-creatordate --format "%(refname:short)" refs/tags')
  .toString()
  .trim();

// The current tag on a tag build is authoritative: an unparseable value here is
// a hard failure, since we cannot derive a version from it.
function parseCurrentTag(tag) {
  const parsed = semver.parse(tag);
  if (parsed === null || parsed.loose) {
    setFailed(`Git tag "${tag}" is not a valid semver version`);
    process.exit();
  }

  return parsed;
}

const gitTagsArray = gitTagsList
  .split('\n')
  .map((tag) => tag.trim())
  .filter((tag) => tag !== '');

// Existing repo tags are best-effort: silently skip any that are not strict
// semver (legacy or malformed tags) rather than failing the entire build, which
// convertGitTagToSemver used to do on the first bad tag it mapped over.
const releasesArray = gitTagsArray
  .map((tag) => semver.parse(tag))
  .filter((parsed) => parsed !== null && !parsed.loose);

// Fall back to 0.0.0 when there are no valid tags yet (fresh repo / all tags
// filtered out).
const latestRelease = isGitTag
  ? parseCurrentTag(gitRef.split('/')[2])
  : (releasesArray[0] ?? semver.parse('0.0.0'));

function isStableRelease(release) {
  return release.prerelease.length === 0 || release.prerelease[0] === 'stable';
}
function isBetaRelease(release) {
  return release.prerelease.length > 0 && ['rc', 'beta'].includes(release.prerelease[0]);
}
function isDevRelease(release) {
  return release.prerelease.length > 0 && ['dev', 'develop'].includes(release.prerelease[0]);
}

// Build version string.
//
// The base MAJOR.MINOR.PATCH comes from release-tool (status mode), which is the
// single source of truth for the version bump: it reads .changes/ and returns
// the *next* version. For branch/PR builds we label the artifact as a
// pre-release of that upcoming version (e.g. 1.6.0-develop+sha) rather than the
// last released tag. We fall back to the latest tag when there are no pending
// changes (RELEASE_SKIP=true) or the value is absent, and always for tag builds
// (the tag itself is authoritative there).
const nextVersion = process.env.RELEASE_NEXT_VERSION;
const releaseSkip = process.env.RELEASE_SKIP === 'true';
const latestBase = `${latestRelease.major}.${latestRelease.minor}.${latestRelease.patch}`;

let currentVersion =
  !isGitTag && nextVersion && !releaseSkip ? nextVersion : latestBase;
if (!isGitTag) {
  // Get last part of branch name and replace all non-alphanumeric characters with dashes
  let sanitizedGitHeadRefName = gitHeadRefName
    .split('/')
    .pop()
    .replace(/[^a-zA-Z0-9-]/g, '-');

  // Remove leading and trailing dashes
  sanitizedGitHeadRefName = sanitizedGitHeadRefName.replace(/^\-+|\-+$/g, '');

  if (sanitizedGitHeadRefName.length > 0) {
    currentVersion += `-${sanitizedGitHeadRefName}`;
  }

  // Add the git commit hash to the version string
  currentVersion += `+${gitShortCommitHash}`;
} else {
  if (latestRelease.prerelease.length > 0) {
    currentVersion += `-${latestRelease.prerelease.join('.')}`;
  }
  if (latestRelease.build.length > 0) {
    currentVersion += `+${latestRelease.build.join('.')}`;
  }
}

// Get the channel to deploy to
let currentChannel;
if (gitHeadRefName === 'master' || (isGitTag && isStableRelease(latestRelease))) {
  currentChannel = 'stable';
} else if (gitHeadRefName === 'beta' || (isGitTag && isBetaRelease(latestRelease))) {
  currentChannel = 'beta';
} else if (gitHeadRefName === 'develop' || (isGitTag && isDevRelease(latestRelease))) {
  currentChannel = 'develop';
} else {
  currentChannel = gitHeadRefName.replace(/[^a-zA-Z0-9-]/g, '-').replace(/^\-+|\-+$/g, '');
}

// CHANGELOG.md validation lives in release-tool (mode: check, see
// check-changes.yml); get-vars only derives the version, channel and boards.

// Make sure we have the files we need
if (!fs.existsSync('platformio.ini')) {
  setFailed('File "platformio.ini" not found');
  process.exit();
}

// Read files
const platformioIniStr = fs.readFileSync('platformio.ini', 'utf8').trim();

// Parse platformio.ini and extract the different boards
const platformioIni = ini.parse(platformioIniStr);

// Get every key that starts with "env:", and that isnt "env:fs" (which is the filesystem) or "env:ci-build" (which is for CI CodeQL and cppcheck)
const boards = Object.keys(platformioIni)
  .filter((key) => key.startsWith('env:') && key !== 'env:fs' && key !== 'env:ci-build')
  .reduce((arr, key) => {
    arr.push(key.substring(4));
    return arr;
  }, []);

console.log('Version:  ' + currentVersion);
console.log('Channel:  ' + currentChannel);
console.log('Boards:   ' + boards.join(', '));
console.log('Tags:     ' + gitTagsArray.join(', '));

// Set outputs
setOutput('version', currentVersion);
setOutput('release-channel', currentChannel);
setOutput('board-list', boards.join('\n'));
setOutput('board-matrix', JSON.stringify({ board: boards }));
