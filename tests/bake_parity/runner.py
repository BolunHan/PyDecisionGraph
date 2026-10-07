"""The parity runner: run a case's three arms in sequence, then compare them.

Each arm is a test file of its own, so each runs in a process of its own - which
is what makes the transcripts separable and the artifacts readable. This script
runs them in the order a case declares (the capi baseline first, then the two
bake arms), keeps every run's output under ``artifacts/``, and only then reads
the transcripts back and compares them.

Comparing the TRANSCRIPTS rather than the objects is the point. The three arms
never share a process, so nothing they produce can be compared in memory; what
they can share is what they WROTE, and a comparison over that is one a human can
redo by reading the files after a failure. It also means a transcript is the
whole contract: an observation no arm was asked for is visible as a difference,
not silently absent.

What is compared is what a correct graph must agree on - the leaf each walk
selected, its signal, how many nodes the walk crossed and the shape of the walk.
What an arm says about ITSELF (which door it used, what its own bake report
said) is written under ``arm.`` and compared by nobody. Each arm also asserts
the design's own expected leaf before it writes anything, so two arms agreeing
on a wrong answer is still a failure.

Run it directly::

    ~/Projects/venv_313/bin/python tests/bake_parity/runner.py

It exits non-zero when an arm fails or the arms disagree, so it is usable as a
check on its own; ``tests/test_bake_parity.py`` is the thin wrapper that
puts it in the suite.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from parity import ARTIFACTS, comparable, read_transcript, transcript_path  # noqa: E402

# The cases, and each case's arms IN THE ORDER THEY MUST RUN: the baseline
# first, then the two bake arms. The order matters because an arm that fails
# early leaves no transcript, and the arms after it are then compared against
# nothing - running the baseline first makes that visible in the report.
CASES = {
    '001_basic_tree': (
        ('capi', 'test_001_basic_tree_capi.py'),
        ('bake', 'test_001_basic_tree_bake.py'),
        ('bake_cabi', 'test_001_basic_tree_bake_cabi.py'),
    ),
    '002_store_driven_tree': (
        ('capi', 'test_002_store_driven_tree_capi.py'),
        ('bake', 'test_002_store_driven_tree_bake.py'),
        ('bake_cabi', 'test_002_store_driven_tree_bake_cabi.py'),
    ),
}


def run_arm(group: str, case: str, filename: str) -> int:
    """Run one arm's test file, keeping its pytest output as an artifact."""
    log_path = os.path.join(ARTIFACTS, group, f'{case}.pytest.log')
    os.makedirs(os.path.dirname(log_path), exist_ok=True)

    result = subprocess.run(
        [sys.executable, '-m', 'pytest', os.path.join(HERE, filename), '-q'],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    with open(log_path, 'w', encoding='utf-8') as fh:
        fh.write(result.stdout)
        if result.stderr:
            fh.write('\n--- stderr ---\n')
            fh.write(result.stderr)
    return result.returncode


def compare(group: str, arms) -> list:
    """Compare the arms' transcripts, returning one problem per disagreement."""
    problems = []
    transcripts = {}

    for case, _ in arms:
        path = transcript_path(case, group)
        if not os.path.exists(path):
            problems.append(f'{case}: no transcript at {path} - the arm did not run to the end')
            continue
        transcripts[case] = comparable(read_transcript(path))

    if len(transcripts) < 2:
        return problems

    reference_case = arms[0][0]
    reference = transcripts.get(reference_case)
    if reference is None:
        return problems

    for case, observations in transcripts.items():
        if case == reference_case:
            continue
        for key in sorted(set(reference) | set(observations)):
            want = reference.get(key, '<absent>')
            got = observations.get(key, '<absent>')
            if want != got:
                problems.append(f'{key}: {reference_case}={want!r} but {case}={got!r}')

    return problems


def main(argv=None) -> int:
    groups = list(argv) if argv else list(CASES)
    failed = False

    for group in groups:
        arms = CASES[group]
        group_failed = False
        print(f'\n=== case {group} ===')

        for index, (case, filename) in enumerate(arms, start=1):
            label = 'abc'[index - 1]
            print(f'  [{label}] {case:<10} ', end='', flush=True)
            code = run_arm(group, case, filename)
            if code != 0:
                group_failed = True
                failed = True
                print(f'ARM FAILED (exit {code}) - see artifacts/{group}/{case}.pytest.log')
            else:
                print('ok')

        if group_failed:
            # An arm that died mid-walk leaves a truncated transcript, and
            # diffing against it would report every line it never reached as a
            # disagreement - which is noise on top of the one real failure.
            print('  comparison skipped: an arm did not finish (see its pytest log)')
            continue

        problems = compare(group, arms)
        if problems:
            failed = True
            print(f'  -- {len(problems)} disagreement(s):')
            for problem in problems:
                print(f'     {problem}')
        else:
            shared = comparable(read_transcript(transcript_path(arms[0][0], group)))
            print(f'  all {len(arms)} arms agree on {len(shared)} observations')

    print('\nPARITY OK' if not failed else '\nPARITY FAILED')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
