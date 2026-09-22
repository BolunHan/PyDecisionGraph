from libcpp cimport bool as c_bool

from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol
from cbase.bytemap.c_bytemap cimport BoundByteMap, bytemap

from .c_hierarchy cimport dcg_breakpoint_node, dcg_root_node
from .c_node cimport LogicNode, dcg_node


cdef extern from "decision_graph/decision_tree/bake/c_logic_group.h":
    const size_t DCG_LGM_DEFAULT_CAPACITY

    ctypedef enum dcg_logic_group_type:
        DCG_LG_BASE
        DCG_LG_MAPPING

    ctypedef struct dcg_logic_group:
        dcg_logic_group_type lgtype
        const char* name
        dcg_logic_group* parent

    ctypedef struct dcg_lgm_state:
        dcg_logic_group** groups
        size_t n_groups
        size_t groups_capacity
        dcg_node** nodes
        size_t n_nodes
        size_t nodes_capacity
        dcg_breakpoint_node** breakpoints
        size_t n_breakpoints
        size_t breakpoints_capacity
        c_bool inspection_mode
        c_bool vigilant_mode

    ctypedef struct dcg_action_node:
        pass

    ctypedef enum dcg_node_type:
        DCG_NODE_NOACTION

    ctypedef struct dcg_logic_group_manager:
        allocator_protocol* allocator
        bytemap registry
        dcg_logic_group** groups
        size_t n_groups
        size_t groups_capacity
        dcg_node** nodes
        size_t n_nodes
        size_t nodes_capacity
        dcg_breakpoint_node** breakpoints
        size_t n_breakpoints
        size_t breakpoints_capacity
        dcg_lgm_state* shelved
        size_t n_shelved
        size_t shelved_capacity
        c_bool inspection_mode
        c_bool vigilant_mode

    const char* c_dcg_logic_group_type_name(dcg_logic_group_type lgtype) noexcept nogil

    void* c_dcg_lgm_reserve(void** block, size_t* capacity, size_t count, size_t element_size, allocator_protocol* allocator) noexcept nogil
    int c_dcg_lgm_push_group(dcg_logic_group_manager* mgr, dcg_logic_group* group) noexcept nogil
    int c_dcg_lgm_push_node(dcg_logic_group_manager* mgr, dcg_node* node) noexcept nogil
    int c_dcg_lgm_push_breakpoint(dcg_logic_group_manager* mgr, dcg_breakpoint_node* breakpoint) noexcept nogil

    int c_dcg_logic_group_init(dcg_logic_group* group, dcg_logic_group_type lgtype, const char* name) noexcept nogil
    dcg_logic_group* c_dcg_logic_group_new(dcg_logic_group_type lgtype, const char* name, allocator_protocol* allocator) noexcept nogil
    void c_dcg_logic_group_dealloc(dcg_logic_group* group) noexcept nogil
    void c_dcg_logic_group_free(dcg_logic_group* group) noexcept nogil

    dcg_logic_group_manager* c_dcg_lgm_new(allocator_protocol* allocator) noexcept nogil
    int c_dcg_lgm_init(dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil
    void c_dcg_lgm_dealloc(dcg_logic_group_manager* mgr) noexcept nogil
    void c_dcg_lgm_free(dcg_logic_group_manager* mgr) noexcept nogil
    void c_dcg_lgm_clear(dcg_logic_group_manager* mgr) noexcept nogil

    int c_dcg_lgm_register(dcg_logic_group_manager* mgr, dcg_logic_group* group) noexcept nogil
    dcg_logic_group* c_dcg_lgm_find(const dcg_logic_group_manager* mgr, const char* name, size_t name_len) noexcept nogil

    int c_dcg_lgm_enter_group(dcg_logic_group_manager* mgr, dcg_logic_group* group) noexcept nogil
    int c_dcg_lgm_exit_group(dcg_logic_group_manager* mgr, dcg_logic_group* group) noexcept nogil
    int c_dcg_lgm_enter_node(dcg_logic_group_manager* mgr, dcg_node* node) noexcept nogil
    int c_dcg_lgm_connect_awaiting(dcg_logic_group_manager* mgr, dcg_node* node) noexcept nogil
    int c_dcg_lgm_exit_node(dcg_logic_group_manager* mgr, dcg_node* node) noexcept nogil
    int c_dcg_lgm_label_node(dcg_logic_group_manager* mgr, dcg_node* node) noexcept nogil
    int c_dcg_lgm_break_inspection(dcg_logic_group_manager* mgr, dcg_logic_group* group) noexcept nogil

    int c_dcg_node_root_ctx_shelve(dcg_root_node* root, dcg_logic_group_manager* mgr) noexcept nogil
    int c_dcg_node_root_ctx_unshelve(dcg_root_node* root, dcg_logic_group_manager* mgr) noexcept nogil
    int c_dcg_lgm_shelve(dcg_logic_group_manager* mgr) noexcept nogil
    int c_dcg_lgm_unshelve(dcg_logic_group_manager* mgr) noexcept nogil

    dcg_logic_group* c_dcg_lgm_active_group(const dcg_logic_group_manager* mgr) noexcept nogil
    dcg_node* c_dcg_lgm_active_node(const dcg_logic_group_manager* mgr) noexcept nogil
    size_t c_dcg_lgm_breakpoint_count(const dcg_logic_group_manager* mgr) noexcept nogil

    int c_dcg_node_auto_connect(dcg_node* node, dcg_logic_group_manager* mgr) noexcept nogil
    dcg_action_node* c_dcg_node_connect_new_action(dcg_node_type action_type, const char* repr, c_bool auto_connect, ssize_t sig, void* action_data, dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil
    dcg_action_node* c_dcg_node_connect_new_action_trade(dcg_node_type action_type, c_bool auto_connect, dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil
    dcg_action_node* c_dcg_node_connect_new_action_clear(c_bool auto_connect, dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil
    dcg_node* c_dcg_node_connect_new_placeholder(c_bool auto_connect, dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil


cdef class LogicGroupNameRegistry(BoundByteMap):
    cdef void* _ws_key_buf

    @staticmethod
    cdef LogicGroupNameRegistry c_from_header(dcg_logic_group_manager* header, bint owner=?)


cdef class LogicGroupManager:
    cdef dcg_logic_group_manager* header
    cdef bint owner

    cdef list node_stack

    cdef readonly LogicGroupNameRegistry registry

    cdef void c_register(self, dcg_logic_group* group)

    cdef dcg_logic_group* c_find(self, const char* name)

    cdef void c_enter_group(self, dcg_logic_group* group)

    cdef void c_exit_group(self, dcg_logic_group* group)

    cdef int c_label_node(self, dcg_node* node)

    cdef void c_break_inspection(self, dcg_logic_group* group)

    cdef void c_shelve(self)

    cdef void c_unshelve(self)

    cpdef void node_stack_append(self, LogicNode node)

    cpdef void node_stack_pop(self, LogicNode node)


cdef LogicGroupManager LGM


cdef class LogicGroup:
    cdef dcg_logic_group* header
    cdef bint owner

    @staticmethod
    cdef inline LogicGroup c_from_header(dcg_logic_group* header, bint owner=False)

    cdef void c_free_header(self)

    cdef void c_break(self, dcg_logic_group* scope)


cdef class LogicGroupWrapperRegistry(BoundByteMap):
    cdef void* _ws_key_buf

    @staticmethod
    cdef LogicGroupWrapperRegistry c_from_header(bytemap* header, bint owner=?)

cdef LogicGroupWrapperRegistry GROUP_REGISTRY
