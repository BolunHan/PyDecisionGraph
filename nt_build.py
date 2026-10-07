#!/usr/bin/env python3
"""
NT Build Orchestrator — sync, build, and test PyDecisionGraph on a remote Windows VM via SSH.

Reads connection info from nt_config.json (same directory as this script).

Usage:
    python nt_build.py                  # full cycle: sync → build → test
    python nt_build.py --sync-only      # sync project files only
    python nt_build.py --build-only     # build only (assumes already synced)
    python nt_build.py --test-only      # test only (assumes already built)
    python nt_build.py --skip-sync      # build + test, skip sync
    python nt_build.py -v               # verbose SSH output
"""

import json
import os
import sys
import tempfile
import subprocess
import argparse
from pathlib import Path


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def load_config(config_path):
    """Load and validate nt_config.json."""
    with open(config_path) as f:
        cfg = json.load(f)

    required = ["connection", "windows", "project"]
    for key in required:
        if key not in cfg:
            raise ValueError(f"Missing required key in config: {key}")

    conn = cfg["connection"]
    for field in ("host", "user", "identity_file"):
        if field not in conn:
            raise ValueError(f"Missing connection.{field} in config")

    # Expand tilde in identity_file
    conn["identity_file"] = os.path.expanduser(conn["identity_file"])
    if not os.path.isfile(conn["identity_file"]):
        raise FileNotFoundError(f"Identity file not found: {conn['identity_file']}")

    # Validate project paths
    cfg["project"]["local_root"] = os.path.abspath(
        os.path.expanduser(cfg["project"].get("local_root", os.path.dirname(config_path)))
    )

    return cfg


def load_private_key(path):
    """Try known paramiko key types in sequence."""
    from paramiko import RSAKey, Ed25519Key, ECDSAKey
    for cls in (Ed25519Key, RSAKey, ECDSAKey):
        try:
            return cls.from_private_key_file(path)
        except Exception:
            continue
    raise ValueError(f"Could not load key from {path} — unsupported format?")


def _parse_ssh_config(host_alias):
    """Extract HostName / User / IdentityFile from ~/.ssh/config for *host_alias*."""
    ssh_config_path = Path.home() / ".ssh" / "config"
    if not ssh_config_path.exists():
        return None, None, None

    hostname, user, identity_file = None, None, None
    in_block = False
    with open(ssh_config_path) as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.lower().startswith("host "):
                in_block = (host_alias.lower() in [w.lower() for w in line.split()[1:]])
                continue
            if not in_block:
                continue
            key, _, val = line.partition(" ")
            key, val = key.strip().lower(), val.strip()
            if key == "hostname":
                hostname = val
            elif key == "user":
                user = val
            elif key == "identityfile":
                identity_file = os.path.expanduser(val)
        return hostname, user, identity_file


def cmd_ok(message):
    print(f"\033[32m[OK]\033[0m {message}")


def cmd_info(message):
    print(f"\033[36m[..]\033[0m {message}")


def cmd_warn(message):
    print(f"\033[33m[!!]\033[0m {message}")


def cmd_fail(message):
    print(f"\033[31m[FAIL]\033[0m {message}")


# ---------------------------------------------------------------------------
# SSH / SFTP helpers
# ---------------------------------------------------------------------------

class NTClient:
    """Wrap paramiko SSHClient + SFTP for a single remote session."""

    def __init__(self, cfg, verbose=False):
        self.cfg = cfg
        self.conn = cfg["connection"]
        self.ssh = None
        self.sftp = None
        self.verbose = verbose
        self._connect()

    def _connect(self):
        import paramiko
        paramiko.util.log_to_file(os.devnull)  # suppress paramiko noise
        print(f"Connecting to {self.conn['host']}:{self.conn.get('port', 22)} as {self.conn['user']} ...")
        self.ssh = paramiko.SSHClient()
        self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            self.ssh.connect(
                hostname=self.conn["host"],
                port=self.conn.get("port", 22),
                username=self.conn["user"],
                pkey=load_private_key(self.conn["identity_file"]),
                look_for_keys=False,
                allow_agent=False,
                timeout=15,
            )
        except paramiko.AuthenticationException:
            cmd_fail("Authentication failed. Check identity file and user.")
            raise
        self.sftp = self.ssh.open_sftp()
        cmd_ok("SSH connected")

    def close(self):
        if self.sftp:
            self.sftp.close()
        if self.ssh:
            self.ssh.close()
        print("SSH session closed.")

    def exec_cmd(self, command, capture=False):
        """Execute a command via cmd.exe on the remote Windows host.

        Returns (exit_code, stdout_str, stderr_str).
        PowerShell invocation is used internally when running .ps1 scripts.
        """
        if self.verbose:
            cmd_info(f"exec: {command[:120]}{'...' if len(command) > 120 else ''}")

        _in, _out, _err = self.ssh.exec_command(command, timeout=1800)
        stdout = _out.read().decode("utf-8", errors="replace")
        stderr = _err.read().decode("utf-8", errors="replace")
        exit_code = _out.channel.recv_exit_status()

        if self.verbose or exit_code != 0:
            if stdout.strip():
                print(stdout.strip())
            if stderr.strip():
                print(stderr.strip(), file=sys.stderr)

        return exit_code, stdout, stderr

    def exec_powershell(self, ps_command):
        """Run a PowerShell command and return (exit_code, stdout, stderr)."""
        wrapped = (
            f'powershell.exe -NoProfile -ExecutionPolicy Bypass '
            f'-Command "{ps_command}"'
        )
        return self.exec_cmd(wrapped)

    def put_file(self, local_path, remote_path):
        """Upload a single file via SFTP."""
        if self.verbose:
            cmd_info(f"sftp put: {local_path} -> {remote_path}")
        self.sftp.put(local_path, remote_path)

    def file_exists(self, remote_path):
        """Check if a remote path exists via SFTP stat."""
        try:
            self.sftp.stat(remote_path)
            return True
        except FileNotFoundError:
            return False


# ---------------------------------------------------------------------------
# Sync
# ---------------------------------------------------------------------------

def sync_project(cfg, client: NTClient):
    """Tar the project locally, upload, and extract on the remote side."""
    local_root = cfg["project"]["local_root"]
    remote_root = cfg["project"]["remote_root"]
    remote_parent = remote_root.rsplit("\\", 1)[0]
    exclude = cfg.get("sync", {}).get("exclude_patterns", [])

    # Build tar exclusion arguments
    exclude_args = []
    for pat in exclude:
        exclude_args.extend(["--exclude", pat])

    # Create tarball
    tarball = os.path.join(tempfile.gettempdir(), "pydecisiongraph_nt_sync.tar.gz")
    project_name = os.path.basename(local_root)
    parent_dir = os.path.dirname(local_root)

    cmd_info(f"Creating tarball of {local_root} ...")
    subprocess.run(
        ["tar", "czf", tarball] + exclude_args + ["-C", parent_dir, project_name],
        check=True,
    )
    size_mb = os.path.getsize(tarball) / (1024 * 1024)
    cmd_ok(f"Tarball created ({size_mb:.1f} MB)")

    # Upload
    remote_tarball = os.path.join(
        remote_parent.replace("\\", "/"),
        "pydecisiongraph_nt_sync.tar.gz",
    ).replace("\\", "/")
    cmd_info(f"Uploading to {remote_tarball} ...")
    client.put_file(tarball, remote_tarball)
    cmd_ok("Upload complete")

    # Remote extract
    cmd_info("Extracting on remote ...")
    # Mirror semantics: remove the old tree, extract fresh, delete the tarball.
    # Done in PowerShell — cmd.exe `if ... && ...` chains have precedence and
    # quoting pitfalls (the && binds into the if-body, silently skipping steps).
    extract_ps = (
        f"$ErrorActionPreference='Stop'; "
        f"if (Test-Path '{remote_root}') {{ Remove-Item -Recurse -Force '{remote_root}' }}; "
        f"tar xzf '{remote_tarball}' -C '{remote_parent}'; "
        f"if ($LASTEXITCODE -ne 0) {{ exit $LASTEXITCODE }}; "
        f"Remove-Item -Force '{remote_tarball}'"
    )
    exit_code, stdout, stderr = client.exec_powershell(extract_ps)
    if exit_code != 0:
        cmd_fail(f"Extract failed (exit {exit_code})")
        if stdout.strip():
            print(stdout.strip())
        if stderr.strip():
            print(stderr.strip(), file=sys.stderr)
        return False
    cmd_ok("Sync complete")

    # Clean up local tarball
    os.unlink(tarball)
    return True


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def build_on_remote(cfg, client: NTClient):
    """Run build.ps1 on the remote Windows host."""
    remote_root = cfg["project"]["remote_root"]
    venv_path = cfg["windows"]["venv_path"]
    ps1_path = f"{remote_root}\\build.ps1".replace("/", "\\")

    if not client.file_exists(ps1_path.replace("\\", "/")):
        cmd_fail(f"build.ps1 not found on remote: {ps1_path}")
        return False

    cmd_info(f"Running build.ps1 (venv={venv_path}) ...")
    wrapped = (
        f'powershell.exe -NoProfile -ExecutionPolicy Bypass '
        f'-File "{ps1_path}" -VenvPath "{venv_path}"'
    )
    exit_code, stdout, stderr = client.exec_cmd(wrapped)

    if stdout.strip():
        print(stdout.strip())
    if stderr.strip():
        print(stderr.strip(), file=sys.stderr)

    if exit_code != 0:
        cmd_fail(f"Build failed (exit {exit_code})")
        return False

    cmd_ok("Build successful")
    return True


# ---------------------------------------------------------------------------
# Test
# ---------------------------------------------------------------------------

def test_on_remote(cfg, client: NTClient):
    """Run the suite on the remote Windows host through nt_steps.ps1 (pytest)."""
    remote_root = cfg["project"]["remote_root"]
    steps_ps1 = f"{remote_root}\\nt_steps.ps1".replace("/", "\\")

    if not client.file_exists(steps_ps1.replace("\\", "/")):
        cmd_fail(f"nt_steps.ps1 not found on remote: {steps_ps1}")
        return False

    # -Config is passed explicitly: $MyInvocation.MyCommand.Path is null under
    # -File over SSH, so the script's own default cannot resolve nt_config.json.
    config_path = f"{remote_root}\\nt_config.json".replace("/", "\\")
    cmd_info("Running nt_steps.ps1 test_src (pytest) ...")
    wrapped = (
        f'powershell.exe -NoProfile -ExecutionPolicy Bypass '
        f'-File "{steps_ps1}" test_src -Config "{config_path}"'
    )
    exit_code, stdout, stderr = client.exec_cmd(wrapped)

    if stdout.strip():
        print(stdout.strip())
    if stderr.strip():
        print(stderr.strip(), file=sys.stderr)

    if exit_code != 0:
        cmd_fail(f"Tests failed (exit {exit_code})")
        return False

    cmd_ok("All tests passed")
    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="NT Build Orchestrator — sync, build, test PyDecisionGraph on Windows VM"
    )
    parser.add_argument(
        "--config", "-c",
        default=None,
        help="Path to nt_config.json (default: <script_dir>/nt_config.json)",
    )
    parser.add_argument(
        "--sync-only", action="store_true",
        help="Sync project files only, then exit",
    )
    parser.add_argument(
        "--build-only", action="store_true",
        help="Build only (assumes already synced)",
    )
    parser.add_argument(
        "--test-only", action="store_true",
        help="Test only (assumes already built)",
    )
    parser.add_argument(
        "--skip-sync", action="store_true",
        help="Skip sync, run build + test",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Verbose output (show all SSH commands)",
    )
    parser.add_argument(
        "--host",
        default=None,
        help="SSH host alias from ~/.ssh/config (e.g. Win11-CN). "
             "If given, connection fields are resolved from the alias.",
    )
    parser.add_argument(
        "--step",
        default=None,
        help="Run one nt_steps.ps1 step on the remote instead (info | install | "
             "ensure_pytest | ensure_deps | test_src | test_installed).",
    )
    args = parser.parse_args()

    # Resolve config path
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = args.config or os.path.join(script_dir, "nt_config.json")
    if not os.path.isfile(config_path):
        cmd_fail(f"Config not found: {config_path}")
        sys.exit(1)

    cfg = load_config(config_path)

    # Optionally overlay SSH config alias
    if args.host:
        hostname, user, identity_file = _parse_ssh_config(args.host)
        if hostname:
            cfg["connection"]["host"] = hostname
        if user:
            cfg["connection"]["user"] = user
        if identity_file:
            cfg["connection"]["identity_file"] = identity_file
            if not os.path.isfile(identity_file):
                cmd_warn(f"IdentityFile from ssh config not found: {identity_file}")

    # Determine which steps to run
    do_sync = True
    do_build = True
    do_test = True

    if args.sync_only:
        do_build = do_test = False
    elif args.build_only:
        do_sync = do_test = False
    elif args.test_only:
        do_sync = do_build = False
    elif args.skip_sync:
        do_sync = False

    # === Execute ===
    print("=" * 60)
    print(" PyDecisionGraph NT Build Orchestrator")
    print(f" Target: {cfg['connection']['host']}:{cfg['connection'].get('port', 22)}")
    print(f" Remote: {cfg['project']['remote_root']}")
    print(f" Steps: sync={do_sync} build={do_build} test={do_test}")
    print("=" * 60)

    client = None
    success = True
    try:
        client = NTClient(cfg, verbose=args.verbose)

        if args.step:
            steps_ps1 = f"{cfg['project']['remote_root']}\\nt_steps.ps1".replace("/", "\\")
            config_path = f"{cfg['project']['remote_root']}\\nt_config.json".replace("/", "\\")
            cmd_info(f"Running nt_steps.ps1 {args.step} ...")
            exit_code, stdout, stderr = client.exec_cmd(
                f'powershell.exe -NoProfile -ExecutionPolicy Bypass '
                f'-File "{steps_ps1}" {args.step} -Config "{config_path}"'
            )
            if stdout.strip():
                print(stdout.strip())
            if stderr.strip():
                print(stderr.strip(), file=sys.stderr)
            success = exit_code == 0
        else:
            if do_sync:
                print("\n--- Sync ---")
                if not sync_project(cfg, client):
                    success = False

            if do_build and success:
                print("\n--- Build ---")
                if not build_on_remote(cfg, client):
                    success = False

            if do_test and success:
                print("\n--- Test ---")
                if not test_on_remote(cfg, client):
                    success = False

    except Exception as exc:
        cmd_fail(str(exc))
        if args.verbose:
            import traceback
            traceback.print_exc()
        success = False
    finally:
        if client:
            client.close()

    print()
    if success:
        cmd_ok("NT build pipeline finished successfully.")
        sys.exit(0)
    else:
        cmd_fail("NT build pipeline failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
