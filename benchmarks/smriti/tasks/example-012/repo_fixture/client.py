import os


def get_api_key():
    """Return this service's API credential, read from wherever this
    project's deployment scripts and CI already export it -- not
    necessarily the generic name you'd guess first."""
    raise NotImplementedError
