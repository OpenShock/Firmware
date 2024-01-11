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

if (!isGitTag && !isGitBranch && !isGitPullRequest) {
  core.setFailed(`Git ref "${gitRef}" is not a valid branch, tag or pull request`);
  process.exit();
}

const gitCommitHash = process.env.GITHUB_SHA;
const gitShortCommitHash = child_process.execSync('git rev-parse --short HEAD').toString().trim();

if (gitCommitHash === undefined) {
  core.setFailed('Environment variable "GITHUB_SHA" not found');
  process.exit();
}

const gitHeadRefName = isGitPullRequest ? process.env.GITHUB_HEAD_REF : gitRef.split('/')[2];
if (gitHeadRefName === undefined) {
  core.setFailed('Failed to get git head ref name');
  process.exit();
}

const gitTagsList = child_process.execSync('git for-each-ref --sort=-creatordate --format "%(refname:short)" refs/tags').toString().trim();
if (gitTagsList === undefined) {
  core.setFailed('Failed to get latest git tag');
  process.exit();
}

function convertGitTagToSemver(tag) {
  const parsed = semver.parse(tag === '' ? '0.0.0' : tag);
  if (parsed === null || parsed.loose) {
    core.setFailed(`Git tag "${tag}" is not a valid semver version`);
    process.exit();
  }

  return parsed;
}

const gitTagsArray = gitTagsList.split('\n').map((tag) => tag.trim());
const releasesArray = gitTagsArray.map(convertGitTagToSemver);
const latestRelease = isGitTag ? convertGitTagToSemver(gitRef.split('/')[2]) : releasesArray[0];

const stableReleasesArray = releasesArray.filter((release) => release.prerelease.length === 0 || release.prerelease[0] === 'stable');
const betaReleasesArray = releasesArray.filter((release) => release.prerelease.length > 0 && ['rc', 'beta'].includes(release.prerelease[0]));
const devReleasesArray = releasesArray.filter((release) => release.prerelease.length > 0 && ['dev', 'develop'].includes(release.prerelease[0]));

// Build version string
let currentVersion = `${latestRelease.major}.${latestRelease.minor}.${latestRelease.patch}`;
if (!isGitTag) {
  // Get last part of branch name and replace all non-alphanumeric characters with dashes
  let sanitizedGitHeadRefName = gitHeadRefName
    .split('/')
    .pop()
    .replace(/[^a-zA-Z0-9-]/g, '-');

  // Remove leading and trailing dashes
  sanitizedGitHeadRefName = sanitizedGitHeadRefName.replace(/^\-+|\-+$/g, '');

  // If the branch name is 'develop', use 'dev' instead
  if (sanitizedGitHeadRefName === 'develop') {
    sanitizedGitHeadRefName = 'dev';
  }

  if (sanitizedGitHeadRefName.length > 0) {
    currentVersion += `-${sanitizedGitHeadRefName}`;
  }

  // Add the git commit hash to the version string
  currentVersion += `+${gitShortCommitHash}`;
}

// Get the channel to deploy to
let currentChannel;
if (gitHeadRefName === 'master') {
  currentChannel = 'stable';
} else if (gitHeadRefName === 'develop') {
  currentChannel = 'dev';
} else if (gitHeadRefName === 'beta') {
  currentChannel = 'beta';
} else {
  currentChannel = gitHeadRefName.replace(/[^a-zA-Z0-9-]/g, '-').replace(/^\-+|\-+$/g, '');
}

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
  const changeLogBegin = lines.findIndex((line) => line.startsWith(`# Version ${currentVersion}`));
  if (isGitTag && changeLogBegin === -1) {
    core.setFailed(`File "CHANGELOG.md" does not contain a changelog entry for version "${currentVersion}", this must be added in the master branch`);
    process.exit();
  }

  // Enforce that the changelog entry is at the top of the file if we are on the master branch
  if (isGitTag && changeLogBegin !== 0) {
    core.setFailed(`Changelog entry for version "${currentVersion}" is not at the top of the file, you tag is either out of date or you have not updated the changelog`);
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
    core.setFailed(`Changelog entry for version "${currentVersion}" is empty, this must be populated in the master branch`);
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
const fullChangelog = fs.readFileSync('CHANGELOG.md', 'utf8').trim();
const platformioIniStr = fs.readFileSync('platformio.ini', 'utf8').trim();

const fullChangelogLines = fullChangelog.split('\n');

// Get all versions from the changelog
const changelogVersions = fullChangelogLines.filter((line) => line.startsWith('# Version ')).map((line) => line.substring(10).split(' ')[0].trim());

// Get the changelog for the current version
const versionChangeLog = getVersionChangeLog(fullChangelogLines);

// Enforce that all tags exist in the changelog
let missingTags = [];
for (const tag of gitTagsArray) {
  if (!changelogVersions.includes(tag)) {
    missingTags.push(tag);
  }
}
if (missingTags.length > 0) {
  core.setFailed(`Changelog is missing the following tags: ${missingTags.join(', ')}`);
  process.exit();
}

// Finish building the release string
if (versionChangeLog !== '') {
  releaseNotes = `# OpenShock Firmware ${currentVersion}\n\n${versionChangeLog}\n\n${releaseNotes}`.trim();
} else {
  releaseNotes = `# OpenShock Firmware ${currentVersion}\n\n${releaseNotes}`.trim();
}

// Parse platformio.ini and extract the different boards
const platformioIni = ini.parse(platformioIniStr);

// Get every key that starts with "env:", and that isnt "env:fs" (which is the filesystem)
const boards = Object.keys(platformioIni)
  .filter((key) => key.startsWith('env:') && key !== 'env:fs')
  .reduce((arr, key) => {
    arr.push(key.substring(4));
    return arr;
  }, []);

const shouldDeploy = isGitTag || (isGitBranch && gitHeadRefName === 'develop');

console.log('Version:  ' + currentVersion);
console.log('Channel:  ' + currentChannel);
console.log('Boards:   ' + boards.join(', '));
console.log('Deploy:   ' + shouldDeploy);
console.log('Tags:     ' + gitTagsArray.join(', '));
console.log('Stable:   ' + stableReleasesArray.join(', '));
console.log('Beta:     ' + betaReleasesArray.join(', '));
console.log('Dev:      ' + devReleasesArray.join(', '));

// Set outputs
core.setOutput('version', currentVersion);
core.setOutput('changelog', versionChangeLog);
core.setOutput('release-notes', releaseNotes);
core.setOutput('release-channel', currentChannel);
core.setOutput('full-changelog', fullChangelog);
core.setOutput('board-list', boards.join('\n'));
core.setOutput('board-array', JSON.stringify(boards));
core.setOutput('board-matrix', JSON.stringify({ board: boards }));
core.setOutput('should-deploy', shouldDeploy);
core.setOutput('release-stable-list', stableReleasesArray.join('\n'));
core.setOutput('release-stable-array', JSON.stringify(stableReleasesArray));
core.setOutput('release-beta-list', betaReleasesArray.join('\n'));
core.setOutput('release-beta-array', JSON.stringify(betaReleasesArray));
core.setOutput('release-dev-list', devReleasesArray.join('\n'));
core.setOutput('release-dev-array', JSON.stringify(devReleasesArray));
