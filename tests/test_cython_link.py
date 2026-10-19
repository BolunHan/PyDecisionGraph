"""Tests for the Cython/Includes link this package exposes.

The link is what lets a downstream build ``cimport decision_graph...`` without
adding this project to its ``include_path``. Each test builds a throwaway
``site-packages`` layout under ``tmp_path`` so the assertions are about the
helper's behavior, not whatever copy happens to be installed in the test env.
"""

import importlib
import importlib.util
import os
import subprocess
import types
from pathlib import Path

import pytest

from decision_graph._cython_link import ensure_cython_include_link


@pytest.fixture
def layout(tmp_path):
    """A minimal site-packages: the package dir and Cython's Includes dir."""
    package_dir = tmp_path / "site-packages" / "decision_graph"
    package_dir.mkdir(parents=True)
    includes_dir = tmp_path / "site-packages" / "Cython" / "Includes"
    includes_dir.mkdir(parents=True)
    return package_dir, includes_dir


def _link_or_skip(target, link):
    """Create a directory link, skipping the test where the OS forbids it."""
    try:
        os.symlink(target, link, target_is_directory=True)
    except (OSError, NotImplementedError) as exc:
        pytest.skip(f"directory links unavailable: {exc}")


def test_it_creates_a_relative_link_to_the_package(layout):
    package_dir, includes_dir = layout
    link = includes_dir / "decision_graph"

    assert ensure_cython_include_link(package_dir, includes_dir) is True
    assert link.is_symlink()
    assert link.resolve() == package_dir.resolve()
    if os.name == "posix":
        assert os.readlink(link) == os.path.join("..", "..", "decision_graph")


def test_a_second_call_leaves_the_link_untouched(layout):
    package_dir, includes_dir = layout
    link = includes_dir / "decision_graph"
    assert ensure_cython_include_link(package_dir, includes_dir) is True

    before = os.lstat(link)
    assert ensure_cython_include_link(package_dir, includes_dir) is True
    after = os.lstat(link)

    assert (after.st_ino, after.st_mtime_ns) == (before.st_ino, before.st_mtime_ns)


def test_a_symlinked_includes_path_still_points_at_the_package(tmp_path):
    real_env = tmp_path / "a" / "b" / "c" / "deep_env"
    includes_dir = real_env / "Cython" / "Includes"
    includes_dir.mkdir(parents=True)
    linked_env = tmp_path / "short_env"
    _link_or_skip(real_env, linked_env)
    package_dir = tmp_path / "site-packages" / "decision_graph"
    package_dir.mkdir(parents=True)

    assert ensure_cython_include_link(
        package_dir, linked_env / "Cython" / "Includes"
    ) is True
    assert (includes_dir / "decision_graph").resolve() == package_dir.resolve()


def test_it_replaces_a_link_that_points_elsewhere(layout, tmp_path):
    package_dir, includes_dir = layout
    other = tmp_path / "other_package"
    other.mkdir()
    link = includes_dir / "decision_graph"
    _link_or_skip(other, link)

    assert ensure_cython_include_link(package_dir, includes_dir) is True
    assert link.resolve() == package_dir.resolve()


def test_it_leaves_a_real_directory_alone(layout, capsys):
    package_dir, includes_dir = layout
    link = includes_dir / "decision_graph"
    link.mkdir()

    assert ensure_cython_include_link(package_dir, includes_dir) is False
    assert link.is_dir()
    assert not link.is_symlink()
    assert "not a link" in capsys.readouterr().err


def test_missing_cython_is_a_silent_no_op(tmp_path, capsys):
    package_dir = tmp_path / "decision_graph"
    package_dir.mkdir()
    missing_includes = tmp_path / "Cython" / "Includes"

    assert ensure_cython_include_link(package_dir, missing_includes) is False
    assert not (missing_includes / "decision_graph").exists()
    assert capsys.readouterr().err == ""


def test_opt_out_skips_everything(layout, monkeypatch):
    package_dir, includes_dir = layout
    monkeypatch.setenv("DCG_NO_CYTHON_LINK", "1")

    assert ensure_cython_include_link(package_dir, includes_dir) is False
    assert not (includes_dir / "decision_graph").exists()


def test_a_package_dir_that_is_not_a_directory_is_a_no_op(tmp_path):
    includes_dir = tmp_path / "Cython" / "Includes"
    includes_dir.mkdir(parents=True)
    missing_package = tmp_path / "no_such_package"

    assert ensure_cython_include_link(missing_package, includes_dir) is False
    assert not (includes_dir / "decision_graph").exists()


@pytest.mark.skipif(os.name != "posix", reason="POSIX permissions required")
def test_a_read_only_includes_dir_is_not_fatal(layout, capsys):
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        pytest.skip("root can write to a read-only directory")

    package_dir, includes_dir = layout
    includes_dir.chmod(0o555)
    try:
        assert ensure_cython_include_link(package_dir, includes_dir) is False
    finally:
        includes_dir.chmod(0o755)

    assert "could not create" in capsys.readouterr().err


@pytest.mark.skipif(os.name != "posix", reason="simulated junction needs a POSIX link")
def test_windows_falls_back_to_a_junction(layout, monkeypatch):
    import cbase.cython_injection as injection

    package_dir, includes_dir = layout
    link = includes_dir / "decision_graph"
    commands = []
    real_symlink = os.symlink

    def failing_symlink(*args, **kwargs):
        raise OSError("symlink privilege not held")

    def fake_run(command, **kwargs):
        commands.append(command)
        real_symlink(package_dir, link, target_is_directory=True)
        return subprocess.CompletedProcess(command, 0)

    monkeypatch.setattr(injection, "_is_windows", lambda: True)
    monkeypatch.setattr(injection.os, "symlink", failing_symlink)
    monkeypatch.setattr(injection.subprocess, "run", fake_run)

    assert ensure_cython_include_link(package_dir, includes_dir) is True
    assert len(commands) == 1
    assert commands[0][:4] == ["cmd", "/c", "mklink", "/J"]
    assert commands[0][4] == str(link)
    assert commands[0][5] == str(package_dir.resolve())


def _load_setup_module():
    spec = importlib.util.spec_from_file_location(
        "_setup_under_test",
        Path(__file__).resolve().parents[1] / "setup.py",
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_setup_links_inplace_builds_to_the_source_tree(monkeypatch):
    setup = _load_setup_module()
    captured = []

    def fake_inject(package, **kwargs):
        captured.append((package, kwargs))
        return "injected"

    monkeypatch.setattr(setup, "_inject_cython_includes", fake_inject)

    setup.BuildExtWithConfig.inject_cython_link(types.SimpleNamespace(inplace=True))

    package, kwargs = captured[0]
    assert package == setup.PACKAGE_NAME
    assert kwargs["source"] == Path(setup.REPO_ROOT) / setup.PACKAGE_NAME
    assert kwargs["editable"] is True


def test_setup_links_regular_builds_to_the_installed_copy(monkeypatch):
    setup = _load_setup_module()
    captured = []

    def fake_inject(package, **kwargs):
        captured.append((package, kwargs))
        return "injected"

    monkeypatch.setattr(setup, "_inject_cython_includes", fake_inject)

    setup.BuildExtWithConfig.inject_cython_link(types.SimpleNamespace(inplace=False))

    assert captured[0][1]["editable"] is False


def test_importing_the_package_runs_the_hook(monkeypatch):
    import decision_graph
    import decision_graph._cython_link as link_module

    calls = []

    def fake_ensure(*args, **kwargs):
        calls.append((args, kwargs))
        return True

    monkeypatch.setattr(link_module, "ensure_cython_include_link", fake_ensure)
    try:
        importlib.reload(decision_graph)
        assert calls
    finally:
        monkeypatch.undo()
        importlib.reload(decision_graph)
