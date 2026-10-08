#!/usr/bin/env python3
"""Rewrite the API pages' `doxygenclass` blocks as Python-domain declarations.

The pages under ``docs/source/decision_tree/`` describe a class by handing it to
Breathe, which renders every member of the doxygen XML under its BARE name. The
same member name belongs to more than one class - capi's ``LogicExpression`` and
bake's ``ExpressionNode`` both declare ``__add__`` - so Sphinx refuses the second
one: ``duplicate object description of __add__, other instance in ...``.

The name a class member is described under is the stub's, and the stubs are fully
qualified by the ``py:module`` context, so this script declares each class from
its ``.pyi`` instead: one ``py:class`` per class, one directive per member, the
docstrings that the stubs already carry. Every member is described, once.

Run it after the stubs change, then rebuild:

    python docs/gen_reference.py && cd docs/source && sphinx-build -M html . ../_build
"""

from __future__ import annotations

import ast
import glob
import os
import re
import sys
import types

from sphinx.ext.napoleon.docstring import GoogleDocstring  # docs-time only, like the build itself

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGES = os.path.join(REPO, 'docs', 'source', 'decision_tree')

BLOCK = re.compile(r'^(?P<indent>[ \t]*)\.\. doxygenclass:: (?P<fqn>\S+)[ \t]*\n(?P<opts>(?:[ \t]+:\S+:.*\n)*)', re.M)

# Napoleon turns an ``Attributes:`` section into ``.. attribute::`` directives by
# default, and those register the very names this script declares below them.
class _NapoleonConfig(types.SimpleNamespace):
    """Napoleon reads its settings off a Sphinx config; only one of them matters here."""

    def __getattr__(self, name: str):
        if name.startswith('napoleon_'):
            return False  # every switch it asks for that is not set below is off
        raise AttributeError(name)


NAPOLEON = _NapoleonConfig(
    napoleon_use_ivar=True, napoleon_use_param=True, napoleon_use_rtype=True, napoleon_use_keyword=True,
    napoleon_attr_annotations=True, napoleon_google_docstring=True,
    napoleon_type_aliases=None, napoleon_custom_sections=None,
)


def stub_module(path: str) -> str:
    """The dotted module a stub file declares."""
    return os.path.relpath(path, REPO)[:-4].replace(os.sep, '.')


def stub_class(fqn: str) -> ast.ClassDef:
    """The stub's declaration of the class a page names by its doxygen fqn."""
    fqn = fqn.replace('::', '.').replace('-', '_')
    module, name = fqn.rsplit('.', 1)
    path = os.path.join(REPO, module.replace('.', os.sep) + '.pyi')
    if not os.path.exists(path):
        raise FileNotFoundError(f'no stub for {fqn}: expected {path}')
    for node in ast.parse(open(path, encoding='utf-8').read()).body:
        if isinstance(node, ast.ClassDef) and node.name == name:
            return node
    raise LookupError(f'{path} does not declare {name}')


def class_homes() -> dict[str, set[str]]:
    """Every class name a stub declares, and the modules that declare it.

    A name declared in more than one layer (``LogicNode`` in both bake and capi)
    cannot be resolved from its bare form, so an annotation that names one has to
    say which: those are the names this script qualifies.
    """
    homes: dict[str, set[str]] = {}
    for path in glob.glob(os.path.join(REPO, 'decision_graph', '**', '*.pyi'), recursive=True):
        module = stub_module(path)
        for node in ast.parse(open(path, encoding='utf-8').read()).body:
            if isinstance(node, ast.ClassDef):
                homes.setdefault(node.name, set()).add(module)
    return homes


def import_map(path: str) -> dict[str, str]:
    """Local name -> dotted path, from a stub's own imports (relative ones resolved)."""
    module = stub_module(path)
    package = module.rsplit('.', 1)[0]
    names: dict[str, str] = {}
    for node in ast.walk(ast.parse(open(path, encoding='utf-8').read())):
        if isinstance(node, ast.ImportFrom):
            base = node.module or ''
            if node.level:
                parts = package.split('.')[:len(package.split('.')) - node.level + 1]
                base = '.'.join(parts + ([base] if base else []))
            for alias in node.names:
                if not alias.name.startswith('*'):
                    names[alias.asname or alias.name] = f'{base}.{alias.name}'
    return names


def qualify(text: str, module: str, names: dict[str, str], ambiguous: set[str]) -> str:
    """Write each ambiguous class name as a ``~full.path`` cross-reference.

    ``~`` keeps the rendered name short while the target stays unique, which is
    what a signature needs when two layers declare the same class.
    """
    if not text:
        return text

    def replace(match: re.Match) -> str:
        name = match.group(0)
        path = names.get(name) or f'{module}.{name}'
        return '~' + path if name in ambiguous else name

    return re.sub(r'\b[A-Z]\w+\b', replace, text)


def member_lines(node: ast.ClassDef, types_in: "callable") -> list[str]:
    """One directive per member the class declares, in the stub's own order."""
    lines: list[str] = []
    for item in node.body:
        if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if any(isinstance(d, ast.Attribute) and d.attr in ('setter', 'deleter') for d in item.decorator_list):
                continue  # a property's setter describes the same object as the property
            decorators = {d.id for d in item.decorator_list if isinstance(d, ast.Name)}
            prefix = 'py:property' if 'property' in decorators else 'py:method'
            kind = [f'      :{d}:' for d in ('staticmethod', 'classmethod') if d in decorators]
            returns = f' -> {types_in(ast.unparse(item.returns))}' if item.returns else ''
            signature = f'{item.name}{_params(item, types_in)}{returns}'
            lines.append(f'   .. {prefix}:: {signature}')
            lines.extend(kind)
            lines.extend(_docstring(item, types_in))
            lines.append('')
        elif isinstance(item, ast.AnnAssign) and isinstance(item.target, ast.Name):
            lines.append(f'   .. py:attribute:: {item.target.id}')
            if item.value is not None:
                lines.append(f'      :value: {ast.unparse(item.value)}')
            lines.append('')
    while lines and not lines[-1]:
        lines.pop()
    return lines


def _params(node: ast.FunctionDef, types_in: "callable") -> str:
    """The signature as written: annotations, defaults, and the markers that keep the shape."""
    args = node.args
    positional = args.posonlyargs + args.args
    defaults = [None] * (len(positional) - len(args.defaults)) + [ast.unparse(d) for d in args.defaults]
    parts: list[str] = []
    for arg, default in zip(positional, defaults):
        parts.append(_arg(arg, default, types_in))
        if args.posonlyargs and arg is args.posonlyargs[-1]:
            parts.append('/')
    if args.vararg:
        parts.append('*' + ast.unparse(args.vararg))
    elif args.kwonlyargs:
        parts.append('*')
    for arg, default in zip(args.kwonlyargs, [ast.unparse(d) if d else None for d in args.kw_defaults]):
        parts.append(_arg(arg, default, types_in))
    if args.kwarg:
        parts.append('**' + ast.unparse(args.kwarg))
    return '(' + ', '.join(parts) + ')'


def _arg(arg: ast.arg, default: str | None, types_in: "callable") -> str:
    text = arg.arg + (f': {types_in(ast.unparse(arg.annotation))}' if arg.annotation else '')
    return text + (f' = {types_in(default)}' if default is not None else '')


def _docstring(node: ast.AST, types_in: "callable") -> list[str]:
    """The docstring, as restructured text.

    The stubs are written Google-style, and that is not restructured text: a
    section such as ``Args:`` followed by an indented list is a definition list
    to docutils, and ends where the next line says. Napoleon is the converter
    Sphinx itself uses, so the sections come out as field lists; text it does not
    recognize as a section passes through untouched.
    """
    text = ast.get_docstring(node)
    if not text:
        return []
    out = []
    for line in str(GoogleDocstring(text, NAPOLEON)).splitlines():
        # The type of a parameter is the one place in a docstring Sphinx reads as
        # a type, so it is the one place a name has to be qualified.
        if re.match(r'\s*:(vartype|type) \w+:', line):
            head, _, tail = line.partition(': ')
            line = f'{head}: {types_in(tail)}'
        out.append(f'      {line}' if line else '')
    return [''] + out


def block_for(fqn: str, indent: str, homes: dict[str, set[str]]) -> str:
    """The page's replacement text for one `doxygenclass` block."""
    node = stub_class(fqn)
    module = fqn.replace('::', '.').rsplit('.', 1)[0]
    names = import_map(os.path.join(REPO, module.replace('.', os.sep) + '.pyi'))
    ambiguous = {name for name, modules in homes.items() if len(modules) > 1}

    def types_in(text: str) -> str:
        return qualify(text, module, names, ambiguous)

    # The bases are rendered as written - a ``~`` cross-reference is not resolved
    # there - so they are named the way the stub names them.
    bases = ', '.join(ast.unparse(b) for b in node.bases)
    out = [
        f'{indent}.. py:module:: {module}',
        f'{indent}   :no-index:',  # a context, not an object: several pages share a module
        '',
        f'{indent}.. py:class:: {node.name}{f"({bases})" if bases else ""}',
    ]
    out.extend(f'{indent}{line}' if line else '' for line in _docstring(node, types_in))
    out.append('')
    out.extend(f'{indent}{line}' if line else '' for line in member_lines(node, types_in))
    return '\n'.join(out) + '\n'


def rewrite(path: str, homes: dict[str, set[str]]) -> int:
    text = open(path, encoding='utf-8').read()
    count = 0

    def replace(match: re.Match) -> str:
        nonlocal count
        count += 1
        return block_for(match.group('fqn'), match.group('indent'), homes)

    new = BLOCK.sub(replace, text)
    if count:
        open(path, 'w', encoding='utf-8').write(new)
    return count


def main() -> int:
    homes = class_homes()
    total = pages = 0
    for path in sorted(glob.glob(os.path.join(PAGES, '*', '*.rst'))):
        n = rewrite(path, homes)
        if n:
            pages += 1
            total += n
            print(f'  {os.path.relpath(path, REPO)}: {n} class(es)')
    print(f'{total} class directives rewritten across {pages} pages')
    return 0


if __name__ == '__main__':
    sys.exit(main())
