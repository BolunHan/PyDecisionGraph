"""The allocator the bake layer allocates through, and the switches it reads.

Every block the layer builds - nodes, groups, slots, strings - comes from the
allocator protocol, and this module owns the layer's instance of it along with
the switches that decide how it behaves: locking, shared memory, and the free
list.

A switch is read by the C layer as it allocates, so it is set by entering one of
the contexts below rather than by assignment, and a block's lifetime is the
protocol's: a child block dies with its parent.
"""

from cbase.allocator_protocol import AllocatorConfigContext


class ConfigContext(AllocatorConfigContext):
    """A set of allocator switches, applied on entry and put back on exit.

    Attributes:
        overrides: The switches this context sets, keyed by name.
        originals: What those switches were before the context was entered.
    """

    def __init__(self, *, locked: bool = ..., shared: bool = ..., freelist: bool = ...) -> None:
        """Build a context that sets the switches it is given.

        Args:
            locked: Whether allocations take the protocol's lock.
            shared: Whether blocks come from shared memory.
            freelist: Whether freed blocks are recycled.
        """
        ...


class _RuntimeAllocatorConfig:
    """The live switch values, read through properties rather than copied."""

    @property
    def DCG_CFG_LOCKED(self) -> bool:
        """Whether allocations currently take the protocol's lock."""
        ...

    @property
    def DCG_CFG_SHARED(self) -> bool:
        """Whether blocks currently come from shared memory."""
        ...

    @property
    def DCG_CFG_FREELIST(self) -> bool:
        """Whether freed blocks are currently recycled."""
        ...


# The four switch contexts: shared memory, locking, lock-free, free list.
DCG_SHARED: ConfigContext
DCG_LOCKED: ConfigContext
DCG_LOCKFREE: ConfigContext
DCG_FREELIST: ConfigContext

# The switches as they are right now, for a caller that has to ask.
RUNTIME_ALLOCATOR_CONFIG: _RuntimeAllocatorConfig
