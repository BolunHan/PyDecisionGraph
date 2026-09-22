"""The value layer: tagged values, containers, references, casts and output.

The unit of data that flows through a graph is a ``dcg_var_t``: a *tag* and a
*payload*. The tag is what an evaluator dispatches on, the payload is read
through the member the tag names, and everything else in this module is a reader
over the two - which is what ``VarView`` below is the Python window into.

Nothing else here is a Python object: the constructors, the initialisers, the
containers, the references, the casts and the output helpers are all ``cdef``
(``c_dcg_var_*``), plus the ``cdef extern`` declarations of the C header. A
caller reaches a value through the wrapper it belongs to - a node's ``out``, a
store's slot, a condition's payload - and reads it through ``VarView``.

The two Cython-level bridges are not module attributes either:
``c_dcg_var_pypack`` and ``c_dcg_var_pyunpack`` translate between a
``dcg_var_t`` and a Python object, and are cimported by the modules that need
them (``c_const``, ``c_collections``).
"""


class VarView:
    """A read-only window onto a ``dcg_var_t``: the value layer's Python face.

    A view is an ADDRESS, not a copy: it holds the slot it was taken over, and
    every read answers for what that slot holds at the moment of the read. A
    view owns nothing and frees nothing, so it must not outlive the value it
    looks at - which the layer guarantees by handing one out only from a wrapper
    that keeps that value alive (``LogicNode.out``).

    It cannot write, and that is not a promise this docstring is making: the
    view holds the value **const**, and every reader it exposes takes a const
    pointer, so a write through a view does not compile. Reading a value that
    another part of the layer is changing is what a view is for - it reports what
    the slot holds now, never a snapshot taken when it was made.

    The value type itself, which this class documents because it is its subject:

    **The tag.** ``dcg_var_type`` names what the payload is - a raw pointer, a
    string, a bool, a double, an int, an offset, a session time, date or
    datetime, a contiguous double vector, or a matrix - and a tag with reference
    bits set names a REFERENCE instead. The level in those bits is how many hops
    the reference takes, and ``dtype & VAR_TYPE_BASE_MASK`` still names what sits
    at the END of the walk: a reference to a double is a ``double_ref``, a
    reference to that is a ``double_ref_ref``, and one more rung adds one more
    star. A ``reserved`` slot is one that holds nothing yet - an entry created
    before its value - and a reference to one is ``inferred``: the type is the
    slot's to say, and a reader asks the slot.

    **The payload.** One union, read through the member the tag calls for. A
    container tag carries a pointer to a shape struct the value OWNS (allocated
    as a child block, so one free releases the value and its container), a string
    may be owned or borrowed, and a reference carries the address of the slot it
    refers to and nothing else.

    **References.** A reference owns nothing: the slot belongs to whoever set it
    up, must outlive the reference, and must keep the shape the level assumes.
    Reading through one is what the readers do - they follow the hops and read
    what the tag says is at the end - so nothing in a payload is ever read as a
    value header to find the way.

    Attributes:
        _header: The C value this view reads through.
    """

    def __init__(self, address: int) -> None:
        """View the value at an address.

        Args:
            address: Address of a ``dcg_var_t``. The layer hands these out
                itself (``LogicNode.out``); nothing here checks that the address
                is a value, and the view keeps nothing alive.
        """
        ...

    def __repr__(self) -> str:
        """The view's own text: its class and the value's display text."""
        ...

    def format(self) -> str:
        """The value as display text, as the C layer renders it.

        Returns:
            The text: a string quoted, a double with ``%g``, a pointer as an
            address, a bool as true/false, a container as its shape.

        Raises:
            RuntimeError: When the view is uninitialised, or the C layer fails.
        """
        ...

    @property
    def address(self) -> int:
        """The value's address - the slot this view reads through."""
        ...

    @property
    def dtype(self) -> int:
        """The tag, as the enumerator's value (a ``dcg_var_type``)."""
        ...

    @property
    def type_name(self) -> str:
        """The tag's stable name: ``'double'``, ``'double_ref'``, ``'inferred'``."""
        ...

    @property
    def is_null(self) -> bool:
        """Whether the value is absent: no payload of any type.

        A live reference whose slot holds NULL is absent, like a NULL string or a
        vector with no buffer, and so is a reserved slot. A reference that points
        at nothing is not absent but broken: reading through it refuses.
        """
        ...

    @property
    def is_ref(self) -> bool:
        """Whether the tag names a reference rather than a plain value."""
        ...

    @property
    def ref_level(self) -> int:
        """How many hops the tag refers through: 0, 1, 2, ... as far as it nests."""
        ...

    @property
    def ref_base(self) -> int:
        """The tag at the end of the walk - for a plain value, the tag itself."""
        ...

    @property
    def value(self) -> bool | int | float | str | None:
        """The value as a Python object, read by its own tag.

        A reference is read through, as every reader reads one. None comes back
        for a value that stands for nothing - unset, empty, or a tag with no
        Python counterpart (a container, a session time).
        """
        ...

    @property
    def as_bool(self) -> bool:
        """The value's truth, following Python's rules for its tag.

        Never refuses: every tag has a truth value, so this is the one reader
        that can always answer.
        """
        ...

    @property
    def as_int(self) -> int:
        """The value as an integer, coercing: doubles truncate toward zero.

        A non-numeric tag has no answer to give, and the layer's vigilant mode
        reports the mistake where it is made rather than returning a 0 that
        reads like a value.
        """
        ...

    @property
    def as_double(self) -> float:
        """The value as a double, coercing from the numeric tags.

        As with ``as_int``: a tag that is not a number is reported, not coerced.
        """
        ...

    @property
    def as_string(self) -> str | None:
        """The text a string-shaped tag holds, or None when that text is NULL.

        A tag that is not string-shaped is refused rather than answered
        (vigilant mode names the reader and the tag on stderr and aborts), so a
        caller asks ``type_name`` or ``is_null`` first.
        """
        ...

    @property
    def as_ptr(self) -> int:
        """The pointer a pointer-shaped tag holds, as an address."""
        ...

    @property
    def as_ref(self) -> int:
        """The address a reference tag holds - the slot, one star per rung."""
        ...
