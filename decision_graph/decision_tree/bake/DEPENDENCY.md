# The bake wrapper's dependency graph

**What this is.** The **Cython wrapper graph**: which layer each module sits in,
which direction an edge may point, and the ways to reach a symbol the layering
says you may not name. It exists so a move between modules - or a new module -
is decided by the layer table rather than by what happens to compile today.

It is the *wrapper* graph only. The C headers have an include order of their
own; that is not recorded here, and it does not decide where a `.pxd` goes.

**The rule.** Three statements. Every breach in this layer is one of them.

1. **Downward only.** A module may use the layer below it, and nothing above it.
2. **Ordered inside a layer.** A layer is a band of concrete structures, and
   those have a sub-dependency graph of their own: a module may use another
   module of its own layer, and a later sub-layer may use an earlier one. What
   is forbidden inside a layer is a *cycle*, not a sibling - the sub-graph has
   to be acyclic exactly as the whole is.
3. **A cycle is a bug, not a preference.** Cython answers a cimport cycle with
   an *empty* namespace, so the failure surfaces as `'.../c_foo.pxd' not found`
   or as `Variable type 'dcg_node' is incomplete` - neither of which names the
   cycle that caused it.

---

## 1. The layers

| Layer | Sub | Module | Binds (its C header) | May use |
| --- | --- | --- | --- | --- |
| 0 · ground | | `c_var` | `c_var.h` | cbase |
| 1 · node | 1.0 | `c_edge` | `c_edge.h` | ground |
| 1 · node | 1.1 | `c_node` | `c_node.h` | node 1.0, ground |
| 2 · node variant | 2.0 | `c_action` | `c_action.h` | node layer |
| 2 · node variant | 2.0 | `c_const` | `c_const.h` | node layer, ground |
| 2 · node variant | 2.0 | `c_expr` | `c_expr.h` | node layer, ground |
| 2 · node variant | 2.1 | `c_hierarchy` | `c_hierarchy.h` | node variant 2.0, node layer, ground |
| 3 · logic group | | `c_logic_group` | `c_logic_group.h` | node variant layer, node layer |
| 4 · collections | | `c_collections` | `c_collections.h` | logic group layer, node variant layer, ground |
| 5 · reconstruct | | `c_reconstruct` | — (Cython only) | every layer below it |
| 6 · protocol | 6.0 | `c_eval` | `c_eval.h` | every layer below it |
| 6 · protocol | 6.1 | `c_bake` | `c_bake.h` | every layer below it |

`c_reconstruct` binds no C header: its work is entirely Cython-side - a C node
pointer in, a wrapper out - and it is the one module that maps a node's **type**
to the class that wraps it. That map is the whole of why it sits on top: it
names every family, so every family is below it, and the layer it may use is all
of them. The cost is the other half of the same fact: nothing below can name
*it*, which is why the node layer's callback and registry reach it the long way
round (3.3) rather than by a cimport.

Its opposite number is the fallback it does not name either: `c_node`'s
`LogicNode.c_from_header` builds the generic view - a wrapper of the node base
over the block - and that is what the reconstruction answers with for a type
nothing claims.

`c_bake` sits above even that one, and for the same kind of reason: the pass it
binds is asked of a ROOT and reads the whole layer to answer - the graph's shape
(node layer), the operands of its operators and the rules they carry (node
variant layer), and the stores its reads name (collections) - so every layer is
below it. It is the one module that reaches `c_hierarchy` without being reached
by it: a root does not know what a bake is, and `RootLogicNode.bake` is a lazy
import away from the module that does (3.3).

The table states what a module **may** reach. Section 3 lists what it reaches
**today**; the two are not the same thing, and an edge that disappears from
section 3 does not remove the permission. A module may also reach another module
of its own layer - rule 2.

## 2. The graph

```
 layer 6 · protocol      ┌───────────────────────▼───────────────────────┐
                         │  c_bake                                (6.1)  │
                         └───────────────────────┬───────────────────────┘
                                                 │
                         ┌───────────────────────▼───────────────────────┐
                         │  c_eval                                (6.0)  │
                         └───────────────────────┬───────────────────────┘
                                                 │
 layer 5 · reconstruct   ┌───────────────────────▼───────────────────────┐
                         │  c_reconstruct                                │
                         └───────────────────────┬───────────────────────┘
                                                 │
 layer 4 · collections   ┌───────────────────────▼───────────────────────┐
                         │  c_collections                                │
                         └───────────────────────┬───────────────────────┘
                                                 │
 layer 3 · logic group   ┌───────────────────────▼───────────────────────┐
                         │  c_logic_group                                │
                         └───────────────────────┬───────────────────────┘
                                                 │
 layer 2 · node variant  ┌───────────────────────▼───────────────────────┐
                         │  c_hierarchy                           (2.1)  │
                         └───────────────────────┬───────────────────────┘
                                                 │
                               ┌─────────────────┼─────────────────┐
                               │                 │                 │
                         ┌─────▼─────┐     ┌─────▼─────┐     ┌─────▼─────┐
                         │  c_action │     │  c_const  │     │  c_expr   │
                         └─────┬─────┘     └─────┬─────┘     └─────┬─────┘
                               └─────────────────┼─────────────────┘
                                                 │
 layer 1 · node          ┌───────────────────────▼───────────────────────┐
                         │  c_node                                (1.1)  │
                         └───────────────────────┬───────────────────────┘
                                                 │
 layer 1 · node          ┌───────────────────────▼───────────────────────┐
                         │  c_edge                                (1.0)  │
                         └───────────────────────┬───────────────────────┘
                                                 │
 layer 0 · ground        ┌───────────────────────▼───────────────────────┐
                         │  c_var                                        │
                         └───────────────────────┬───────────────────────┘
                                                 ▼
                                               cbase
```

## 3. The edges as they stand

### 3.1 Direct cimports

Every line below points at its own layer or one beneath it, and the whole is
acyclic. An edge inside a layer is a sub-dependency, which rule 2 allows;
nothing here points upward, and every upward reach in the layer is textual (3.2)
or a lazy import (3.3).

```
c_var.pxd          ──▶  cbase
c_edge.pxd         ──▶  c_var
c_node.pxd         ──▶  c_edge, c_var
c_action.pxd       ──▶  c_node
c_const.pxd        ──▶  c_expr, c_node, c_var
c_expr.pxd         ──▶  c_node, c_var
c_hierarchy.pxd    ──▶  c_node, c_var
c_logic_group.pxd  ──▶  c_hierarchy, c_node
c_collections.pxd  ──▶  c_const, c_logic_group, c_var
c_reconstruct.pxd  ──▶  c_action, c_collections, c_const, c_expr, c_hierarchy, c_logic_group, c_node
c_eval.pxd         ──▶  c_hierarchy, c_node, c_var
c_bake.pxd         ──▶  c_hierarchy, c_node, c_var
```

A module also reaches `cbase` for `allocator_protocol`, and `cbase.bytemap` for
`bytemap` - both from below the wrapper graph, so neither is an edge in it.

One edge is a `.pyx`'s rather than a `.pxd`'s: **`c_expr.pyx` cimports
`c_dcg_node_pypack` from `c_const.pxd`** - the conversion that turns a Python
value into the literal that carries it, so a composition may be written
`5 + read` and not only `ConstantNode(5) + read`. With `c_const.pxd` reaching
`c_expr.pxd` for the expression classes, that closes a 2-cycle inside layer 2 -
the one shape rule 2 forbids. It compiles and it imports because the helper is a
cdef FUNCTION: Cython emits a runtime type import for every cdef CLASS declared
in a cimported pxd, and a function needs none, so the runtime graph - the one
that has to be a DAG - is untouched. The constraint that follows: nothing on that
edge may name a class of the other module, or `c_expr` starts importing
`c_const`'s types during its own init, which is the failure 3.3 describes.

### 3.2 Textual edges (`cdef extern from`)

These create no `pxd` edge at all, which is what makes them the tool for the
reaches that point upward (section 4.1).

| Site | Declares, from | Points |
| --- | --- | --- |
| `c_edge.pxd` | `DCG_NODE_STRING_MAXLEN`, from `c_node.h` | up, inside the node layer |
| `c_node.pxd` | `c_dcg_node_free_generic`, from `c_hierarchy.h` | up, 1 -> 2.1 |
| `c_node.pxd` | `c_dcg_lgm_enter_node`, `c_dcg_lgm_exit_node`, and an incomplete `dcg_logic_group_manager`, from `c_logic_group.h` | up, 1 -> 3 |
| `c_node.pxd` | `c_dcg_node_eval`, the eval stage flags and codes, from `c_eval.h` | up, 1 -> the eval protocol |
| `c_action.pxd` | the connect constructors, and an incomplete `dcg_logic_group_manager`, from `c_logic_group.h` | up, 2 -> 3 |
| `c_const.pxd` | an incomplete `dcg_logic_group`, from `c_node.h` | up, named but never dereferenced |
| `c_hierarchy.pxd` | an incomplete `dcg_logic_group`, from `c_logic_group.h` | up, named but never dereferenced |
| `c_var.pxd` | an incomplete `dcg_node`, from `c_node.h` | up, named but never dereferenced |
| `c_hierarchy.pxd` | `c_dcg_root_node_eval`, `c_dcg_node_eval_graph`, and the path's outcome fields, from `c_eval.h` | up, 2.1 -> the eval protocol |

### 3.2.1 `c_eval.h` — the protocol, and the declaration module that owns it

The evaluation protocol is a **C header above every family**: it includes the
layer it is written in terms of (the ground family, the root types, and the
operator family's rule). `c_eval.pxd` declares that header's symbols - the record,
the walk and the four doors - completely, and it is a **declaration module**: there
is no `c_eval.pyx`, so nothing in it is a runtime import, and the hub re-exports
the lot (3.4).

What it is not is a module the layers BELOW can cimport. Its declarations name
types from three of them (`dcg_node`, `dcg_root_node` and the record, `dcg_var_t`),
so a cimport from any of those would close a cycle - and `c_node.pxd` and
`c_hierarchy.pxd`, which are the modules that need it, reach it the way 4.1
prescribes: a textual `cdef extern from`. Those two rows stay in 3.2, and what
keeps them there is the direction rather than a missing `pxd`.

The header that module declares has an include graph of its own, where it sits at
the top - and **one family includes it back**: an expression's operands are other
NODES, so producing their values means running them, and running a node is the
protocol's. `c_expr.h` therefore includes `c_eval.h` - a cycle, and a contained
one: the protocol needs nothing from that family but its rule entry, and the family
needs nothing from the protocol but `c_dcg_node_eval`. It is safe because of where
the two edges sit (below each header's own declarations, so either entry order
resolves - `c_hierarchy.h` included `c_expr.h` at the top once, and `c_eval.h` got
a half-parsed `c_hierarchy.h` for it) and because the call site is
forward-declared.

**What the protocol is, and what it is not.** The protocol is the walk, the three
stages, and the record - and the RULES it runs live with the families they are
about (`c_dcg_node_mapping_var_node_eval_hook` in `c_collections.h`, and one rule
per operator in `c_expr.h` - `c_dcg_node_expr_eval_add`, `_ternary`, `_call` and
their siblings, listed flat in `DCG_EXPR_EVAL_FNS`). That is what lets a family
teach its nodes their own evaluation as they are built (`DCG_EVAL_DIRECT_HOOKS`),
and it is why `c_eval.h` holds nothing but the protocol: a rule that lived here
would be one the family could not install. Two rules are on their nodes in EVERY
build, because the dispatch has no business finding them: a store's read carries
the store's rule, and an operator node's rule is injected under the switch and
found from the code without it (`c_dcg_node_expr_eval`).

The same reading decides what the protocol does NOT touch: an operator node's
operands, and the workspace they are read from, are that node's own fields, so the
loop that runs each component and fills each slot is the family's
(`c_dcg_node_expr_eval_operands`), reached from the rule that needs it - not from
the walk.

The incomplete `dcg_logic_group` is the interesting one, and `c_node.h` forwards
it on purpose: a variable node reads a group's store and a breakpoint breaks out
of one, so both must be able to **name** the type - but both reach it by pointer
only, and never dereference it. Neither needs `c_logic_group.pxd`, so neither
takes an upward edge that section 4 would have to carry instead.

### 3.3 The reach that is not an edge

No `pxd` cimports upward today. The one upward reach the node layer needs is a
lazy import, recorded here rather than left to be rediscovered.

**`c_node.pyx` - the manager, by lazy import.** Entering and leaving a node means
telling the manager, and a cimport is what closed the cycle: `c_node.pyx` pulled
`c_logic_group.pxd`, which reaches `c_hierarchy.pxd`, which declares the `cdef`
classes `RootLogicNode` and `BreakpointNode`. Cython emits a runtime type import
for every `cdef` class declared in a cimported `pxd` - named by the importer or
not - so `c_node` was importing its own subclasses' module during its own init.
The failure was order-dependent, and it read as something else entirely:

```
AttributeError: partially initialized module '.../c_hierarchy' has no attribute
'RootLogicNode' (most likely due to a circular import)
```

It reaches the manager the long way round instead, as section 4 prescribes:
`c_logic_group.h` is externed for the node's enter and exit (4.1), and the
manager itself arrives by a lazy import inside `c_get_manager` (4.3), cached in a
module-level pointer after the first call. **The node layer therefore holds no
cimport into layer 3 at all**, and the layer's runtime import graph - the thing
that actually has to be acyclic - is a DAG.

**`c_hierarchy.pyx` does not take that reach either.** A breakpoint names the
group it breaks out of, and it holds that as a plain Python object with a public
address (4.2) rather than naming `LogicGroup`, which would be an edge from 2.1
up into 3. The wrapper keeps the object, so the borrowed pointer cannot outlive
it.

**`c_node.pyx` - the wrapper's class, by lazy import.** A node reached from C has
to come back as the class it is, and that map lives in `c_reconstruct`, the one
module above every family (layer 5). The node layer cannot cimport it, so it
reaches the module's Python door at call time - `c_dcg_node_wrapper`, the same
shape as `c_get_manager` above, and cached the same way - and it is reached from
both places that hand a node back: `NODE_REGISTRY`'s lookup path and the callback
adaptor that fills a parent's `children`. The rebuild is unchanged in kind: what
the class is comes back from above, and the block a wrapper stands for stays
this layer's.

The reach is one way and it terminates: `c_reconstruct` asks `NODE_REGISTRY` for
a node it already holds (a membership test, never the lookup, which would come
straight back here), and builds only what the registry does not have.

**`c_var.pyx` - the node layer's door, by lazy import.** A value can hold a NODE
(`VAR_TYPE_NODE`: an action's slot carries the node it stands for), and what such
a value unpacks to is that node's WRAPPER - which is the node layer's to hand out,
one above this one. The reach is `NODE_REGISTRY`, taken at call time inside
`c_dcg_var_pyunpack`, and the class check in `c_dcg_var_pypack` takes the same
reach for `LogicNode`. The value layer still names no node type in C: the pointer
crosses as a `const dcg_node*` the layer never dereferences (see
`c_dcg_var_init_node`), and only the wrapper layer's Python door turns it back
into something with a class.

**`c_hierarchy.pyx` - the bake protocol, by lazy import.** A bake is asked of a
root, and the pass that answers reads the whole layer: it is the highest module
there is, and the root's own module is three layers below it. `RootLogicNode.bake`
reaches the door at call time - `from .c_bake import c_dcg_bake_root` - so the
edge the SOURCE shows (`c_bake` cimports `c_hierarchy` for the type of the root it
is handed) is the only one the runtime graph has, and it points down.


`tests/test_bake_imports.py` holds this: each module is imported first, in a
fresh interpreter. An upward edge can still be added, and that suite is where it
fails.

### 3.4 The hub: `__infra__.pxd`

Every symbol this package exposes to a consumer is re-exported by `__infra__.pxd`
- one block per module, in that module's own order - and a `.pxd` change is not
done until the hub has it. It is the export surface rather than a module of the
table: nothing in the layer cimports it, so it can name every layer at once
without taking part in the graph.

Two things about it are worth stating, because both have been got wrong:

- it carries the **declaration modules too**, `c_eval` among them - a consumer that
  wants the evaluation protocol's doors has no other way to ask for them;
- what it exports is proved by **compiling a probe that cimports through it**, not
  by reading the blocks: the names have to resolve at the C level (the extern
  declarations) and at the Python level (the `cdef` classes, which pull their own
  modules in).

## 4. Reaching upward: the three sanctioned tools

A need that points upward is real - a lower layer often has to let a higher one
act - but it may never be expressed as an ordinary cimport. Three ways, in order
of preference.

### 4.1 `cdef extern from "<header>"` - for C symbols

Textual: it declares the C symbol again in this translation unit and creates no
dependency on any `pxd`, so no cycle can form. The declaration then has to be
kept in step by hand - that is the price, and it is why it is reserved for
*stable* things. Section 3.2 is the full inventory.

The connect constructors are the worked example. They take a
`dcg_logic_group_manager`, which is a complete type only in `c_logic_group.h` -
and a `static inline` has to be **defined** in every translation unit that
**declares** it, so a function whose body needs that type cannot be declared
anywhere lower. The two headers therefore cannot swap roles: declared in
`c_action.h` (the family's shape), defined in `c_logic_group.h`. `c_action.pxd`
reaches them by extern; a cimport would point up from layer 2 into layer 3.

### 4.2 `cdef object` + a public address - for classes

A `cdef class` cannot be declared by an extern block. Naming one in a `pxd` *is*
a hard dependency, and that is exactly what forms cycles; holding it as a plain
Python object does not.

- `NODE_REGISTRY` (`c_node.pyx`) and `GROUP_REGISTRY` (`c_logic_group.pyx`) - the
  wrapper a block already has, keyed by the block's address. This is the layer's
  identity rule: one block, one wrapper, and a lookup that misses answers with a
  wrapper of its own.
- `LogicNode.address` / `LogicNode.c_from_header` (`c_node.pyx`) - the wrapper's
  public handle on its C pointer, and the generic view built over one. The
  manager hands back the active node by address, because it cannot name the node
  module at import time without closing the cycle; the node layer does the
  wrapping - and which CLASS the wrapper is, is `c_reconstruct`'s to say, not the
  node layer's.

A registry lookup that finds nothing **raises**. It never falls back silently.

### 4.3 Lazy import - for the Python surface

A `def` method or property may import a sibling at call time. Nothing is
resolved at module load, so no cycle exists. For a *pure Python* need only - a
cimport inside a function is still a compile-time edge.

## 5. Placing a new module

1. Find the layer by what the module needs to **name**, not by what it does.
2. If it needs something above that layer, use section 4. If section 4 will not
   carry it, it belongs in a higher layer - say so, and let the table move.
3. Add the row here in the same change. A module that is not in the table has no
   layer, and one without a layer is how the next cycle gets written.
