"""The CI trigger, checked against the names it must and must not match.

A rule ``if:`` takes an EXPRESSION. A bare ``/regex/`` is not matched against the
ref or the tag - GitLab reads it as a non-empty string, and a non-empty string is
true. The version-tag condition was written that way once, and every pipeline then
carried the publish jobs: a push to ``main`` uploaded wheels to PyPI and raced the
tag's own upload, which failed on files that already existed.

Nothing about that is visible in the file - it parses, and the regex is right - so
the checks are on the mechanism: every condition must reference a variable, the
publish rules must be the tag condition, and the tag condition must sort the
project's real tags from its real refs.
"""

import re
from pathlib import Path

CI = Path(__file__).resolve().parents[1] / '.gitlab-ci.yml'

# The release scheme's vocabulary - what the tag condition has to admit.
TAGS = ('v0.3.1', 'v0.3.10.post1', 'v0.3.10.alpha2', 'v0.4.0rc1', 'v0.4.0-rc2', 'v0.4.0.dev1')

# Names a tag push never carries, and refs the condition must not claim.
REFS = ('main', 'feature/x', 'v0.3', '0.3.1', 'release-0.3.1')


def _anchors() -> dict[str, str]:
    """The conditions the file keeps in an anchor, by anchor name."""
    return {name: value.strip().strip("'\"") for name, value in re.findall(r'&(\w+)\s+(.+)', CI.read_text())}


def _resolve(entry: str) -> str:
    """One condition as GitLab would read it, quoting and anchor included."""
    value = entry.split('#')[0].strip().strip("'\"")
    return _anchors().get(value[1:], value) if value.startswith('*') else value


def _conditions(block: str) -> list[str]:
    return [_resolve(entry) for entry in re.findall(r'-\s+if:\s*([^\n]+)', block)]


def _block(name: str) -> str:
    """One top-level entry of the file, up to the next one."""
    text = CI.read_text()
    rest = text[text.index(f'\n{name}:'):]
    end = re.search(r'\n\S', rest[1:])
    return rest[:end.start() + 1] if end else rest


def test_the_file_still_declares_conditions():
    """A guard on the guard: the scan has to find the rules it is checking."""
    assert len(_conditions(_block('workflow'))) >= 3, _block('workflow')


def test_every_rule_condition_is_an_expression():
    text = CI.read_text()
    for condition in {_resolve(entry) for entry in re.findall(r'if:\s*([^\n]+)', text)}:
        # An expression begins with a variable - or a parenthesized group of them.
        # A bare regex begins with '/', and is a non-empty string, so it is true.
        assert re.match(r'^\(*\$', condition), f'not an expression, so always true: {condition}'


def test_publish_runs_on_a_version_tag_and_nothing_else():
    conditions = _conditions(_block('.publish'))
    assert conditions, 'the publish job has no rules'
    assert 'CI_COMMIT_TAG' in conditions[0] and '=~' in conditions[0], conditions[0]
    for other in conditions[1:]:
        assert 'CI_COMMIT_BRANCH' not in other, f'a branch push could publish: {other}'


def test_the_workflow_condition_is_the_tag_condition():
    conditions = _conditions(_block('workflow'))
    assert any('CI_COMMIT_TAG' in c and '=~' in c for c in conditions), conditions


def test_the_tag_condition_matches_tags_and_refuses_refs():
    conditions = [_resolve(entry) for entry in re.findall(r'if:\s*([^\n]+)', CI.read_text())]
    matched = [c for c in conditions if '=~' in c]
    assert matched, f'no regex-matched variable condition in the file: {conditions}'
    assert any('CI_COMMIT_TAG' in c for c in matched), matched
    condition = next(c for c in matched if 'CI_COMMIT_TAG' in c)
    pattern = re.compile(re.search(r'=~\s*/(.*)/\s*$', condition).group(1))
    for tag in TAGS:
        assert pattern.match(tag), f'{tag} would not trigger a pipeline'
    for ref in REFS:
        assert not pattern.match(ref), f'{ref} would trigger a pipeline'
