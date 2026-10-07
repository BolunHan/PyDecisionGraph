import types
import dis
import opcode
import sys
import inspect
import ast
import textwrap


def f():
    x = 1
    y = 2
    z = x + y
    print(f"Result: {z}")
    return z


def f2():
    print("🎯 INJECTED FUNCTION CALLED!")
    return "injected"


# --- AST-based injection (safer than raw bytecode patching) ---
src = textwrap.dedent(inspect.getsource(f))
module_ast = ast.parse(src)
# Find the function definition
func_def = None
for node in module_ast.body:
    if isinstance(node, ast.FunctionDef) and node.name == f.__name__:
        func_def = node
        break

if func_def is None:
    raise RuntimeError('Could not find function AST')

# Find the index where y is assigned and insert a Call to f2 after it
insertion_index = None
for i, stmt in enumerate(func_def.body):
    # match simple assignment 'y = <const>'
    if isinstance(stmt, ast.Assign):
        for target in stmt.targets:
            if isinstance(target, ast.Name) and target.id == 'y':
                insertion_index = i + 1
                break
    if insertion_index is not None:
        break

if insertion_index is None:
    raise RuntimeError('Could not find assignment to y in function body')

# Build AST node: Expr(Call(Name('f2'), []))
call_node = ast.Expr(value=ast.Call(func=ast.Name(id='f2', ctx=ast.Load()), args=[], keywords=[]))
# Preserve lineno information roughly
call_node.lineno = func_def.lineno + insertion_index
call_node.col_offset = 0

# Insert the call into the function body
func_def.body.insert(insertion_index, call_node)
ast.fix_missing_locations(module_ast)

# Compile the modified AST in the original globals so f2 is resolvable
compiled = compile(module_ast, filename=f.__code__.co_filename, mode='exec')
new_globals = dict(f.__globals__)
exec(compiled, new_globals)
# Extract the new function object
new_f = new_globals.get(f.__name__)
if not isinstance(new_f, types.FunctionType):
    raise RuntimeError('Failed to compile new function')

print('Original function:')
dis.dis(f)

print('\nModified function (AST injection):')
dis.dis(new_f)

print('\nExecuting modified function:')
res = new_f()
print('Result:', res)

# Assign back to f (optional)
f = new_f
