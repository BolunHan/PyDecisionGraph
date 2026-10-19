"""Expose ``decision_graph`` to Cython's include path in the running environment.

The implementation lives in :mod:`cbase.cython_injection`; this module keeps
the import-time hook name and the ``DCG_NO_CYTHON_LINK`` opt-out used by
``decision_graph/__init__.py``.
"""

import os
import sys
from pathlib import Path

from cbase.cython_injection import (
    ALREADY_INJECTED,
    CONFLICT,
    FAILED,
    INJECTED,
    cython_injection_path,
    inject_cython_includes,
)

PACKAGE_NAME = "decision_graph"
OPT_OUT_ENV = "DCG_NO_CYTHON_LINK"

_warned = set()


def ensure_cython_include_link(package_dir=None, includes_dir=None):
    """Link the package into the environment's ``Cython/Includes`` directory.

    Args:
        package_dir: Directory to expose. Defaults to this package's directory.
        includes_dir: Cython's Includes directory. Discovered from the running
            interpreter when omitted; passing it explicitly is for tests.

    Returns:
        True when the link is correct or was created; False when the hook is
        disabled, the package directory is absent, or the link could not be
        made.
    """
    if _opted_out():
        return False

    package_dir = Path(package_dir) if package_dir is not None else Path(__file__).resolve().parent
    if not package_dir.is_dir():
        return False

    target = {"includes_dir": includes_dir} if includes_dir is not None else {}
    try:
        status = inject_cython_includes(
            PACKAGE_NAME,
            source=package_dir,
            editable=True,
            **target,
        )
    except Exception as exc:  # An import hook must never break the import.
        _warn(f"could not link {PACKAGE_NAME} into Cython/Includes: {exc}")
        return False

    if status in (INJECTED, ALREADY_INJECTED):
        return True
    if status in (CONFLICT, FAILED):
        link = cython_injection_path(PACKAGE_NAME, **target)
        if status == CONFLICT:
            _warn(f"{link} exists and is not a link; leaving it untouched")
        else:
            _warn(f"could not create {link}")
    return False


def _opted_out():
    value = os.environ.get(OPT_OUT_ENV, "")
    return value.strip().lower() not in ("", "0", "false", "no")


def _warn(message):
    if message in _warned:
        return
    _warned.add(message)
    try:
        print(f"[{PACKAGE_NAME}] {message}", file=sys.stderr)
    except Exception:
        pass
