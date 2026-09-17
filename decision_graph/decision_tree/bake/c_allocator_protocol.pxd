from libcpp cimport bool as c_bool

from cbase.allocator_protocol cimport AllocatorConfigContext, allocator_protocol, heap_allocator, shm_allocator_ctx

cdef c_bool DCG_CFG_LOCKED
cdef c_bool DCG_CFG_SHARED
cdef c_bool DCG_CFG_FREELIST


cdef class ConfigContext(AllocatorConfigContext):
    pass


cdef heap_allocator* HEAP_ALLOCATOR
cdef shm_allocator_ctx* SHM_ALLOCATOR

cdef allocator_protocol* DCG_DEFAULT_ALLOCATOR
cdef allocator_protocol* DCG_SHM_ALLOCATOR
cdef allocator_protocol* DCG_HEAP_ALLOCATOR

cdef ConfigContext DCG_SHARED
cdef ConfigContext DCG_LOCKED
cdef ConfigContext DCG_LOCKFREE
cdef ConfigContext DCG_FREELIST
