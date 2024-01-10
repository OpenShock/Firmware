const fs = require('fs');
const ini = require('ini');
const semver = require('semver');
const core = require('@actions/core');
const child_process = require('child_process');

// Get branch name
const gitRef = process.env.GITHUB_REF;
if (gitRef === undefined) {
  core.setFailed('Environment variable "GITHUB_REF" not found');
  process.exit();
}

const isGitTag = gitRef.startsWith('refs/tags/');
const isGitBranch = gitRef.startsWith('refs/heads/');
const isGitPullRequest = gitRef.startsWith('refs/pull/') && gitRef.endsWith('/merge');
const gitCommitHash = process.env.GITHUB_SHA;
const gitShortCommitHash = child_process.execSync('git rev-parse --short HEAD').toString().trim();

if (!isGitTag && !isGitBranch && !isGitPullRequest) {
  core.setFailed(`Git ref "${gitRef}" is not a valid branch, tag or pull request`);
  process.exit();
}
if (gitCommitHash === undefined) {
  core.setFailed('Environment variable "GITHUB_SHA" not found');
  process.exit();
}

const gitHeadRefName = isGitPullRequest ? process.env.GITHUB_HEAD_REF : gitRef.split('/')[2];
if (gitHeadRefName === undefined) {
  core.setFailed('Failed to get git head ref name');
  process.exit();
}

const latestGitTag = isGitTag ? gitRef.split('/')[2] : child_process.execSync('git for-each-ref --sort=-creatordate --count=1 --format "%(refname:short)" refs/tags').toString().trim();
if (latestGitTag === undefined) {
  core.setFailed('Failed to get latest git tag');
  process.exit();
}

function getCurrentVersion() {
  // Parse and validate latest git tag
  const parsed = semver.parse(latestGitTag === '' ? '0.0.0' : latestGitTag);
  if (parsed === null || parsed.loose) {
    core.setFailed(`Latest git tag "${latestGitTag}" is not a valid semver version`);
    process.exit();
  }

  // Build version string
  let base = `${parsed.major}.${parsed.minor}.${parsed.patch}`;
  if (!isGitTag) {
    // Get last part of branch name and replace all non-alphanumeric characters with dashes
    const sanitizedGitHeadRefName = gitHeadRefName
      .split('/')
      .pop()
      .replace(/[^a-zA-Z0-9-]/g, '-');

    base += `-${sanitizedGitHeadRefName}+${gitShortCommitHash}`;
  }

  return base;
}

const version = getCurrentVersion();

function getVersionChangeLog(lines) {
  const emptyChangelog = lines.length === 0;

  // Enforce that the changelog is not empty if we are on the master branch
  if (isGitTag && emptyChangelog) {
    core.setFailed('File "CHANGELOG.md" is empty, this must be populated in the master branch');
    process.exit();
  }

  if (emptyChangelog) {
    return '';
  }

  // Simple validation of the changelog
  if (!lines[0].startsWith('# Version ')) {
    core.setFailed('File "CHANGELOG.md" must start with "# Version <version>" followed by a changelog entry');
    process.exit();
  }

  // Get the start of the entry
  const changeLogBegin = lines.findIndex((line) => line.startsWith(`# Version ${version}`));
  if (isGitTag && changeLogBegin === -1) {
    core.setFailed(`File "CHANGELOG.md" does not contain a changelog entry for version "${version}", this must be added in the master branch`);
    process.exit();
  }

  // Enforce that the changelog entry is at the top of the file if we are on the master branch
  if (isGitTag && changeLogBegin !== 0) {
    core.setFailed(`Changelog entry for version "${version}" is not at the top of the file, you tag is either out of date or you have not updated the changelog`);
    process.exit();
  }

  // Get the end of the entry
  let changeLogEnd = lines.slice(changeLogBegin + 1).findIndex((line) => line.startsWith('# Version '));
  if (changeLogEnd === -1) {
    changeLogEnd = lines.length;
  } else {
    changeLogEnd += changeLogBegin + 1;
  }

  const emptyChangelogEntry = lines.slice(changeLogBegin + 1, changeLogEnd).filter((line) => line.trim() !== '').length === 0;

  // Enforce that the changelog entry is not empty if we are on the master branch
  if (isGitTag && emptyChangelogEntry) {
    core.setFailed(`Changelog entry for version "${version}" is empty, this must be populated in the master branch`);
    process.exit();
  }

  return lines.slice(changeLogBegin + 1, changeLogEnd).join('\n');
}

// Make sure we have all the files we need
for (const file of ['RELEASE.md', 'CHANGELOG.md', 'platformio.ini']) {
  if (!fs.existsSync(file)) {
    core.setFailed(`File "${file}" not found`);
    process.exit();
  }
}

// Read files
let releaseNotes = fs.readFileSync('RELEASE.md', 'utf8');
let fullChangelog = fs.readFileSync('CHANGELOG.md', 'utf8').trim();
let platformioIniStr = fs.readFileSync('platformio.ini', 'utf8').trim();

const changelogLines = fullChangelog.split('\n');

// Get the changelog for the current version
const versionChangeLog = getVersionChangeLog(changelogLines);

// Finish building the release string
if (versionChangeLog !== '') {
  releaseNotes = `# OpenShock Firmware ${version}\n\n${versionChangeLog}\n\n${releaseNotes}`.trim();
} else {
  releaseNotes = `# OpenShock Firmware ${version}\n\n${releaseNotes}`.trim();
}

// Get all versions from the changelog
const releases = changelogLines.filter((line) => line.startsWith('# Version ')).map((line) => line.substring(10).split(' ')[0].trim());

// Parse platformio.ini and extract the different boards
let platformioIni = ini.parse(platformioIniStr);

// Get every key that starts with "env:", and that isnt "env:fs" (which is the filesystem)
let boards = Object.keys(platformioIni)
  .filter((key) => key.startsWith('env:') && key !== 'env:fs')
  .reduce((arr, key) => {
    arr.push(key.substring(4));
    return arr;
  }, []);

console.log('Version:  ' + version);
console.log('Boards:   ' + boards.join(', '));
console.log('Releases: ' + releases.join(', '));

// Set outputs
core.setOutput('version', version);
core.setOutput('changelog', versionChangeLog);
core.setOutput('release-notes', releaseNotes);
core.setOutput('full-changelog', fullChangelog);
core.setOutput('board-list', boards.join('\n'));
core.setOutput('board-array', JSON.stringify(boards));
core.setOutput('board-matrix', JSON.stringify({ board: boards }));
core.setOutput('release-list', releases.join('\n'));
core.setOutput('release-array', JSON.stringify(releases));
