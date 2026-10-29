"""Fixtures every test gets: an environment without the CI variables the scripts read."""

import pytest

# Read by the scripts at call time. A test that needs one sets it; none may leak in from the shell or from Actions,
# where GITHUB_OUTPUT and GITHUB_STEP_SUMMARY would otherwise collect the outputs the tests write.
SCRUBBED_ENV = (
    'CI',
    'GITHUB_ACTIONS',
    'GITHUB_REF',
    'GITHUB_REF_NAME',
    'GITHUB_REF_TYPE',
    'GITHUB_SHA',
    'GITHUB_EVENT_NAME',
    'GITHUB_EVENT_PATH',
    'GITHUB_BASE_REF',
    'GITHUB_HEAD_REF',
    'GITHUB_REPOSITORY',
    'GITHUB_TOKEN',
    'GITHUB_OUTPUT',
    'GITHUB_STEP_SUMMARY',
    'ACTIONS_ID_TOKEN_REQUEST_URL',
    'ACTIONS_ID_TOKEN_REQUEST_TOKEN',
    'OPENSHOCK_BUILD_DIR',
    'OPENSHOCK_FW_VERSION',
    'OPENSHOCK_FW_GIT_COMMIT',
    'PINNED_CERT_EXPIRY_WARN_DAYS',
    'RELEASE_NEXT_VERSION',
    'RELEASE_SKIP',
    'CHANNEL_INPUT',
    'BOARDS_INPUT',
    'BOARDS',
    'RELEASE_ID',
)


@pytest.fixture(autouse=True)
def clean_env(monkeypatch):
    for name in SCRUBBED_ENV:
        monkeypatch.delenv(name, raising=False)
