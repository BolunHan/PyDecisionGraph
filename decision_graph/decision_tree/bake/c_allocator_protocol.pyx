from libc.stdlib cimport calloc

from cbase.allocator_protocol import AP_SHM_ALLOCATOR_DEFAULT_REGION_SIZE, c_heap_allocator_new, c_shm_allocator_new

cdef c_bool DCG_CFG_LOCKED = False
cdef c_bool DCG_CFG_SHARED = True
cdef c_bool DCG_CFG_FREELIST = True


cdef class ConfigContext(AllocatorConfigContext):
    cdef void c_bind(self, allocator_protocol* schematic=NULL):
        self.allocator_schematic = schematic if schematic else DCG_DEFAULT_ALLOCATOR

    cdef void c_activate(self):
        if 'locked' in self.overrides:
            global DCG_CFG_LOCKED
            DCG_CFG_LOCKED = self.overrides['locked']

        if 'shared' in self.overrides:
            global DCG_CFG_SHARED
            DCG_CFG_SHARED = self.overrides['shared']

        if 'freelist' in self.overrides:
            global DCG_CFG_FREELIST
            DCG_CFG_FREELIST = self.overrides['freelist']

        AllocatorConfigContext.c_activate(self)

    cdef void c_deactivate(self):
        if 'locked' in self.originals:
            global DCG_CFG_LOCKED
            DCG_CFG_LOCKED = self.originals.get('locked')

        if 'shared' in self.originals:
            global DCG_CFG_SHARED
            DCG_CFG_SHARED = self.originals.get('shared')

        if 'freelist' in self.originals:
            global DCG_CFG_FREELIST
            DCG_CFG_FREELIST = self.originals.get('freelist')

        AllocatorConfigContext.c_deactivate(self)


cdef heap_allocator* HEAP_ALLOCATOR = c_heap_allocator_new()
if not HEAP_ALLOCATOR:
    raise OSError("Initialize DCG heap allocator failed")

cdef shm_allocator_ctx* SHM_ALLOCATOR = c_shm_allocator_new(AP_SHM_ALLOCATOR_DEFAULT_REGION_SIZE, <char*> b"c_md_shm")
if not SHM_ALLOCATOR:
    raise OSError("Initialize DCG SHM allocator failed (prefix='c_md_shm')")

cdef allocator_protocol* DCG_DEFAULT_ALLOCATOR = <allocator_protocol*> calloc(1, sizeof(allocator_protocol))
cdef allocator_protocol* DCG_SHM_ALLOCATOR     = <allocator_protocol*> calloc(1, sizeof(allocator_protocol))
cdef allocator_protocol* DCG_HEAP_ALLOCATOR    = <allocator_protocol*> calloc(1, sizeof(allocator_protocol))

DCG_DEFAULT_ALLOCATOR.with_lock                = DCG_CFG_LOCKED
DCG_DEFAULT_ALLOCATOR.with_shm                 = DCG_CFG_SHARED
DCG_DEFAULT_ALLOCATOR.with_freelist            = DCG_CFG_FREELIST
DCG_DEFAULT_ALLOCATOR.shm_allocator_ctx        = SHM_ALLOCATOR
DCG_DEFAULT_ALLOCATOR.shm_allocator            = SHM_ALLOCATOR.shm_allocator
DCG_DEFAULT_ALLOCATOR.heap_allocator           = HEAP_ALLOCATOR

DCG_SHM_ALLOCATOR.with_lock                    = True
DCG_SHM_ALLOCATOR.with_shm                     = True
DCG_SHM_ALLOCATOR.with_freelist                = True
DCG_SHM_ALLOCATOR.shm_allocator_ctx            = SHM_ALLOCATOR
DCG_SHM_ALLOCATOR.shm_allocator                = SHM_ALLOCATOR.shm_allocator
DCG_SHM_ALLOCATOR.heap_allocator               = NULL

DCG_HEAP_ALLOCATOR.with_lock                   = True
DCG_HEAP_ALLOCATOR.with_shm                    = False
DCG_HEAP_ALLOCATOR.with_freelist               = True
DCG_HEAP_ALLOCATOR.shm_allocator_ctx           = NULL
DCG_HEAP_ALLOCATOR.shm_allocator               = NULL
DCG_HEAP_ALLOCATOR.heap_allocator              = HEAP_ALLOCATOR

cdef ConfigContext DCG_SHARED                  = ConfigContext(shared=True)
cdef ConfigContext DCG_LOCKED                  = ConfigContext(locked=True)
cdef ConfigContext DCG_LOCKFREE                = ConfigContext(locked=False)
cdef ConfigContext DCG_FREELIST                = ConfigContext(freelist=True)

globals()['DCG_SHARED']                        = DCG_SHARED
globals()['DCG_LOCKED']                        = DCG_LOCKED
globals()['DCG_LOCKFREE']                      = DCG_LOCKFREE
globals()['DCG_FREELIST']                      = DCG_FREELIST


# -- Runtime config accessor (reads live cdef globals) --------------------
class _RuntimeAllocatorConfig:
    """Property-based accessor for live DCG_CFG_* globals.

    Unlike the compile-time CONFIG mappingproxy, these properties read the
    CURRENT value, which may change when an ConfigContext is active.
    """
    @property
    def DCG_CFG_LOCKED(self):
        return DCG_CFG_LOCKED

    @property
    def DCG_CFG_SHARED(self):
        return DCG_CFG_SHARED

    @property
    def DCG_CFG_FREELIST(self):
        return DCG_CFG_FREELIST


RUNTIME_ALLOCATOR_CONFIG = _RuntimeAllocatorConfig()
