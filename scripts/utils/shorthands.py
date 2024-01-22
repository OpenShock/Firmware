import os
from . import sysenv


# The short ref name of the branch or tag that triggered the workflow run.
# This value matches the branch or tag name shown on GitHub. For example, `feature-branch-1`.
def get_github_ref_name():
    return sysenv.get_string('GITHUB_REF_NAME', '')


# The name of the base ref or target branch of the pull request in a workflow run.
# This is only set when the event that triggers a workflow run is either `pull_request`
# or `pull_request_target`. For example, `main`.
def get_github_base_ref():
    return sysenv.get_string('GITHUB_BASE_REF', '')


# The name of the event that triggered the workflow. For example, `workflow_dispatch`.
def get_github_event_name():
    return sysenv.get_string('GITHUB_EVENT_NAME', '')


# Whether the current environment is a Github CI environment.
def is_github_ci():
    return sysenv.get_bool('CI', False) and sysenv.get_bool('GITHUB_ACTIONS', False)


# Whether the current environment, assuming is_github_ci() == True, is caused by a pull request event.
def is_github_pr():
    return get_github_event_name() == 'pull_request'


# Checks whether the event is a pull_request with the specified branch as base_ref.
def is_github_pr_into(branch: str) -> bool:
    return is_github_pr() and get_github_base_ref() == branch


# Checks if a tag is a release tag.
def is_github_tag() -> bool:
    return get_github_base_ref().startswith('refs/tags/')
