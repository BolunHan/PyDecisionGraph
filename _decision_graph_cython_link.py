"""Create ``Cython/Includes/decision_graph`` in the installed environment.

A wheel runs no code at installation time, so the wheel ships
``_decision_graph_cython_link.pth``; ``site`` imports this module at
interpreter startup and it maintains
``<site-packages>/Cython/Includes/decision_graph -> ../../decision_graph``.

The hook is a no-op when Cython is not installed in the environment, when the
link already resolves, or when a real entry occupies the link path.  The link
logic lives in cbase's ``cython_injection`` module, loaded by path so this
hook never imports the cbase package.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path

PACKAGE = "decision_graph"
INJECTION_MODULE = "cython_injection.py"


def _load_injection(site_packages: Path):
    """Load cbase's injection module without importing the cbase package."""
    module_path = site_packages / "cbase" / INJECTION_MODULE
    if not module_path.is_file():
        try:
            spec = importlib.util.find_spec("cbase")
        except (ImportError, ValueError):
            return None
        locations = getattr(spec, "submodule_search_locations", None)
        if not locations:
            return None
        module_path = Path(next(iter(locations))) / INJECTION_MODULE
        if not module_path.is_file():
            return None
    spec = importlib.util.spec_from_file_location("_cbase_cython_injection", module_path)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ensure_cython_include(site_packages: str | os.PathLike[str] | None = None) -> str | None:
    """Create the ``Cython/Includes`` link if this environment needs it.

    Args:
        site_packages: Directory holding the installed package.  Defaults to
            the directory containing this module.

    Returns:
        The injection status, or ``None`` when no link was attempted.
    """
    root = Path(site_packages) if site_packages is not None else Path(__file__).resolve().parent
    module = _load_injection(root)
    if module is None:
        return None
    try:
        return module.ensure_site_link(PACKAGE, site_packages=root)
    except Exception:
        return None


try:
    ensure_cython_include()
except Exception:
    pass
