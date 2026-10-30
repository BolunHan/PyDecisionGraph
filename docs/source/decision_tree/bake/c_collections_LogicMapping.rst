LogicMapping
============

.. py:module:: decision_graph.decision_tree.bake.c_collections
   :no-index:

.. py:class:: LogicMapping(LogicGroup)

      A group that holds values, keyed by name.

      Reading is how entries come to exist: ``mapping[name]`` is a read, not a
      lookup - an entry the store does not hold yet is reserved, and the read
      points at the empty slot, so a graph can be written against a store that is
      filled in later. An entry's type is decided when a value lands in it, which
      is why a read of one is answered by the store rather than by the read's own
      binding.

      A store's shape is its own to close: ``frozen`` refuses a new entry from then
      on, and leaves what the store already holds - values, reads, writes - working.

      What the store holds right now is a separate question from what a graph reads,
      and it has its own answers: ``get`` returns a held entry's value - ``None``
      when nothing has landed in it - and raises ``KeyError`` for a name the store
      does not hold, while ``set_default`` performs the same read and creates a
      missing entry from a default, only when one is given. Neither builds a read
      or reserves an entry.

   .. py:method:: __init__(self, *, name: str | None = None, capacity: int = 0, parent: ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None = None, **kwargs: Any) -> None

      Build a store.

      :param name: The store's name - part of every read's display text, and
                   refused when it is long enough to overflow that text.
      :param capacity: Initial room for entries; the layer's default when 0.
      :param parent: The group it is built inside, if any.

      :raises ValueError: When the name is too long for the display buffer.
      :raises MemoryError: When the block cannot be allocated.

   .. py:method:: __getitem__(self, key: str) -> ~decision_graph.decision_tree.bake.c_collections.AttrExpression

      The read of an entry, reserving the entry when it is new.

      :param key: Entry name.

      :returns: The read - a node of the graph, not the value.

      :raises KeyError: When the store is frozen and the key is new.

   .. py:method:: __getattr__(self, key: str) -> ~decision_graph.decision_tree.bake.c_collections.AttrExpression

      The same read, written as an attribute.

      :param key: Entry name.

      :returns: The read of that entry.

   .. py:method:: __setitem__(self, key: str, value: Any) -> None

      Put a value in an entry, reserving the entry when it is new.

      :param key: Entry name.
      :param value: Value to store: a bool, int, float, str, or a node's value.

      :raises RuntimeError: When the C layer refuses the write.
      :raises KeyError: When the store is frozen and the key is new.

   .. py:method:: __contains__(self, key: str) -> bool

      Whether an entry is held. Asking does not reserve one.

      :param key: Entry name.

   .. py:method:: get(self, key: str) -> Any

      The value an entry holds.

      Unlike ``mapping[key]``, this is a lookup, not a build: a missing entry is
      not reserved, and an entry that holds nothing yet reads ``None``. The two
      states are told apart by the answer: a name the store does not hold raises,
      while a held entry that holds nothing returns ``None``.

      :param key: Entry name.

      :returns: The entry's value; ``None`` when the entry is held but nothing has
                landed in it.

      :raises KeyError: When the store holds no entry of that name.

   .. py:method:: set_default(self, key: str, default: Any = ...) -> Any

      The value an entry holds, writing ``default`` when the entry is missing.

      The read-and-set :meth:`dict.setdefault` performs, over this store: an
      entry the store already holds answers as it is - a reserved entry
      included, which returns ``None`` and is not written - and only a name the
      store does not hold is created, from ``default``.

      When ``default`` is omitted there is nothing to write, so the call is the
      pure lookup :meth:`get` is: a held entry answers, and a miss raises.

      :param key: Entry name.
      :param default: Value a missing entry is created with; omitted, the call
                      only reads.

      :returns: The value the entry held, or ``default`` when it was missing.

      :raises KeyError: When the store holds no entry of that name and no default
          is given.
      :raises PermissionError: When the store is frozen and the entry is missing.

   .. py:method:: __len__(self) -> int

      The entries held, reserved ones included.

   .. py:method:: __bool__(self) -> bool

      True when the store holds an entry, reserved ones included.

   .. py:method:: update(self, *args: Any, **kwargs: Any) -> None

      Write several entries at once, as :meth:`dict.update` does.

      Accepts one positional argument - a mapping, or an iterable of key/value
      pairs - and any number of keyword arguments, and writes each pair the way
      ``store[key] = value`` does: an entry the store does not hold is reserved
      for it.

      :param \*args: At most one mapping or iterable of pairs.
      :param \*\*kwargs: Entries to write, by name.

      :raises TypeError: When more than one positional argument is given.
      :raises RuntimeError: When the C layer refuses a write.
      :raises KeyError: When the store is frozen and a key is new.

   .. py:method:: clear(self) -> None

      Empty the store, entry by entry.

      Every entry is dropped - its value released and its name forgotten -
      while the store itself stays where it was, so ``len`` is 0 afterwards and
      a key the store held is a new entry again. Freezing is not exempt: it
      seals a store's SHAPE against new entries, and this is a store's contents
      leaving.

      The entries are emptied in place rather than the block being moved, so a
      read built over one keeps a valid address - it reports an unfilled entry,
      not rubbish. What it no longer reports is the value that was there.

   .. py:property:: frozen(self) -> bool

      Whether the store is sealed against new entries.

      What the store already holds is unaffected: freezing seals its SHAPE,
      not its contents.
