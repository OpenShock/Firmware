from os import environ
from subprocess import run, PIPE
from random import randint
import configparser
import github

if __name__ != '__main__':
    print('This script is not meant to be imported.')
    exit(1)

github.ensure_ci('This script is meant to be run in GitHub Actions.')


def generate_release_notes():
    git_log = run(
        ['git', 'log', '--pretty=format:%s', 'master...HEAD'], stdout=PIPE, stderr=PIPE, check=True, text=True
    ).stdout.strip()

    return git_log.split('\n')


def github_log_group(name, lines):
    print('::group::{}'.format(name))
    for line in lines:
        print(line)
    print('::endgroup::')


def read_changelog():
    with open('CHANGELOG.md', 'r') as file:
        return file.read()


def read_release_notes():
    with open('RELEASE.md', 'r') as file:
        return file.read()


def read_version():
    with open('VERSION', 'r') as file:
        return file.read().strip()


def read_platformio_config():
    config = configparser.ConfigParser()
    files = config.read('platformio.ini')
    if len(files) == 0:
        print('Failed to read platformio.ini')
        exit(1)
    return config


def get_platformio_boards(config):
    boards: list[str] = []
    for section in config.sections():
        if not section.startswith('env:') or section == 'env:fs':
            continue
        boards.append(section[4:])
    return boards


gitRef = get_git_ref()
gitCommitHash = get_git_hash()
gitEvent = get_git_event()
isGitTag = gitRef.startswith('refs/tags/')
isGitBranch = gitRef.startswith('refs/heads/')
isGitPullRequest = gitEvent == 'pull_request'

if not isGitTag and not isGitBranch and not isGitPullRequest:
    github_set_failed('Git ref "{}" is not a tag, branch or pull request'.format(gitRef))


# Run rev-parse to get the short commit hash
gitCommitHashShort = run(
    ['git', 'rev-parse', '--short', gitCommitHash], stdout=PIPE, stderr=PIPE, check=True, text=True
).stdout.strip()

if gitCommitHash is None or gitCommitHashShort is None:
    github_set_failed('Failed to get commit hash')

gitHeadRefName = environ.get('GITHUB_HEAD_REF')

versionChangeLog = read_changelog()

missingTags = []
for tag in gitTagsArray:
    if tag not in changeLogVersions:
        missingTags.append(tag)

if len(missingTags) > 0:
    github_set_failed('Changelogs is missing the following tags: {}'.format(missingTags.join(', ')))

if len(versionChangeLog) == 0:
    releaseNotes = '# OpenShock Firmware {}\n\n{}\n\n{}'.format(currentVersion, versionChangeLog, releaseNotes)
else:
    releaseNotes = '# OpenShock Firmware {}\n\n{}'.format(currentVersion, releaseNotes)


platformioCfg = read_platformio_config()
boards = get_platformio_boards(platformioCfg)

shouldDeploy = isGitTag or (isGitBranch and gitHeadRefName == 'develop')

github_log_group(
    'Variables',
    [
        'Version:   ' + currentVersion,
        'Channel:   ' + currentChannel,
        'Boards:    ' + boards.join(', '),
        'Deploying: ' + str(shouldDeploy),
        'Tags:      ' + gitTagsArray.join(', '),
        'Stable:    ' + stableReleasesArray.join(', '),
        'Beta:      ' + betaReleasesArray.join(', '),
        'Dev:       ' + devReleasesArray.join(', '),
    ],
)

github_set_output('version', currentVersion)
github_set_output('changelog', versionChangeLog)
github_set_output('release-notes', releaseNotes)
github_set_output('release-channel', currentChannel)
github_set_output('full-changelog', fullChangelog)
github_set_output('board-list', boards.join('\n'))
github_set_output('board-array', boards)  # JSON array
github_set_output('board-matrix', {'board': boards})  # JSON object
github_set_output('should-deploy', str(shouldDeploy))
github_set_output('release-stable-list', stableReleasesArray.join('\n'))
github_set_output('release-stable-array', stableReleasesArray)  # JSON array
github_set_output('release-beta-list', betaReleasesArray.join('\n'))
github_set_output('release-beta-array', betaReleasesArray)  # JSON array
github_set_output('release-dev-list', devReleasesArray.join('\n'))
github_set_output('release-dev-array', devReleasesArray)  # JSON array
