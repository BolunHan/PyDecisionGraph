from libc.stdint cimport uint32_t, uint64_t, uintptr_t
from libc.stdio cimport FILE
from libcpp cimport bool as c_bool

from cbase.allocator_protocol cimport allocator_protocol
from cbase.bytemap cimport BoundByteMap, bytemap

from .c_edge cimport dcg_node_edge_condition, NodeEdgeCondition
from .c_var cimport dcg_ret_code, dcg_var_t


cdef extern from "decision_graph/decision_tree/bake/c_node.h":
    const int DCG_NODE_STRING_MAXLEN
    const int DCG_NODE_RENDER_MAX_DEPTH
    const char* DCG_DEF_REPR_NOACTION

    ctypedef enum dcg_node_type:
        DCG_NODE_INPUT
        DCG_NODE_TRUE
        DCG_NODE_FALSE
        DCG_NODE_DOUBLE
        DCG_NODE_STRING
        DCG_NODE_INT
        DCG_NODE_VARIABLE
        DCG_NODE_OP
        DCG_NODE_UNARY
        DCG_NODE_BINARY
        DCG_NODE_TERNARY
        DCG_NODE_CALL
        DCG_NODE_ACTION
        DCG_NODE_NOACTION
        DCG_NODE_LONGACTION
        DCG_NODE_SHORTACTION
        DCG_NODE_CANCELACTION
        DCG_NODE_CLEARACTION
        DCG_NODE_PLACEHOLDER
        DCG_NODE_SPECIAL
        DCG_NODE_ROOT
        DCG_NODE_BREAKPOINT

    ctypedef enum dcg_node_mask:
        DCG_NODE_FAMILY_MASK
        DCG_NODE_VARIANT_MASK

    ctypedef enum dcg_node_flag:
        DCG_NODE_FLAG_NONE
        DCG_NODE_FLAG_FROZEN
        DCG_NODE_FLAG_VISITED
        DCG_NODE_FLAG_PRUNED

    ctypedef struct dcg_node_label:
        const char* label
        c_bool owned
        dcg_node_label* next

    ctypedef enum dcg_eval_flag:
        DCG_EVAL_FLAG_NONE
        DCG_EVAL_FLAG_SKIP_CHILDREN
        DCG_EVAL_FLAG_CACHED
        DCG_EVAL_FLAG_BREAKPOINT
        DCG_EVAL_FLAG_TRACE

    ctypedef enum dcg_node_hook_type:
        DCG_HOOK_PRE_EVAL
        DCG_HOOK_EVAL
        DCG_HOOK_POST_EVAL
        DCG_HOOK_COUNT

    ctypedef int (*dcg_node_hook_fn)(dcg_node* node, void* user_data) noexcept

    ctypedef struct dcg_node_eval_ctx:
        dcg_node_hook_fn pre_eval_fn
        dcg_node_hook_fn eval_fn
        dcg_node_hook_fn post_eval_fn
        dcg_node_hook_fn type_eval_fn
        void* user_data
        void* run
        uint64_t flags
        dcg_ret_code err_code
        uint32_t stage
        uint64_t eval_seq_id
        size_t depth
        size_t visits

    ctypedef enum dcg_node_event:
        DCG_NODE_EVENT_CHILD_ADDED
        DCG_NODE_EVENT_CHILD_UPDATED
        DCG_NODE_EVENT_CHILD_REMOVED
        DCG_NODE_EVENT_CHILD_CLEARED
        DCG_NODE_EVENT_MODIFIED
        DCG_NODE_EVENT_EVALUATED

    ctypedef void (*dcg_node_callback_fn)(dcg_node_event event, dcg_node* self, dcg_node* subject, uint64_t seq_id, void* user_data) noexcept

    ctypedef struct dcg_node_callback_ctx:
        dcg_node_callback_fn fn
        void* user_data
        uintptr_t id
        dcg_node_callback_ctx* next

    ctypedef unsigned char uuid_t[16]

    ctypedef struct dcg_node_ctx_ops:
        int (*enter_fn)(dcg_node* node, dcg_logic_group_manager* mgr)
        int (*exit_fn)(dcg_node* node, dcg_logic_group_manager* mgr)

    ctypedef struct dcg_node:
        dcg_node_eval_ctx eval_ctx
        dcg_node_ctx_ops ctx_ops
        dcg_var_t out
        dcg_node_type ntype
        const char* repr
        uuid_t uid
        dcg_node_label* labels
        c_bool autogen
        uint32_t flags
        void* user_payload
        dcg_node_callback_ctx* callbacks
        const dcg_node_edge_condition* condition_to_parent
        dcg_node* parent
        dcg_node* children
        dcg_node* next_sibling
        dcg_node* prev_sibling

    ctypedef struct dcg_validate_report:
        dcg_ret_code code
        dcg_node* node
        size_t errors
        size_t warnings
        size_t nodes
        size_t depth

    ctypedef enum dcg_render_style:
        DCG_RENDER_ASCII
        DCG_RENDER_UNICODE

    ctypedef struct dcg_render_opts:
        dcg_render_style style
        size_t max_depth
        c_bool show_uid
        c_bool show_labels
        c_bool show_condition
        c_bool show_out
        c_bool show_flags
        c_bool show_hooks
        c_bool show_children
        c_bool show_address

    ctypedef struct dcg_strbuf:
        char* data
        size_t cap
        size_t used

    const char* c_dcg_node_type_name(dcg_node_type ntype) noexcept nogil
    c_bool c_dcg_node_type_is_input(dcg_node_type ntype) noexcept nogil
    c_bool c_dcg_node_type_is_op(dcg_node_type ntype) noexcept nogil
    c_bool c_dcg_node_type_is_flat(dcg_node_type ntype) noexcept nogil
    c_bool c_dcg_node_type_is_action(dcg_node_type ntype) noexcept nogil
    c_bool c_dcg_node_type_is_special(dcg_node_type ntype) noexcept nogil
    size_t c_dcg_node_type_arity(dcg_node_type ntype) noexcept nogil
    uint64_t c_dcg_node_gen_seq_id(const void* ptr) noexcept nogil
    void c_dcg_node_unlink(dcg_node* node) noexcept nogil
    void c_dcg_node_adopt_condition(dcg_node* node, const dcg_node_edge_condition* condition) noexcept nogil

    int c_dcg_node_init(dcg_node* node, dcg_node_type ntype, const char* repr) noexcept nogil
    dcg_node* c_dcg_node_new(dcg_node_type ntype, const char* repr, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_dealloc(dcg_node* node) noexcept nogil
    void c_dcg_node_free(dcg_node* node) noexcept nogil

    int c_dcg_node_set_repr(dcg_node* node, const char* repr) noexcept nogil
    int c_dcg_node_set_string(dcg_node* node, const char* value) noexcept nogil

    int c_dcg_node_register_eval_hook(dcg_node* node, dcg_node_hook_type hook, dcg_node_hook_fn fn, void* user_data) noexcept nogil
    void c_dcg_node_unregister_eval_hooks(dcg_node* node) noexcept nogil
    int c_dcg_node_register_callback(dcg_node* node, dcg_node_callback_fn fn, void* user_data, uintptr_t* out_id) noexcept nogil
    int c_dcg_node_unregister_callback(dcg_node* node, uintptr_t callback_id) noexcept nogil
    void c_dcg_node_invoke_callbacks(dcg_node* node, dcg_node_event event, dcg_node* subject, uint64_t seq_id) noexcept nogil
    size_t c_dcg_node_callback_count(const dcg_node* node) noexcept nogil

    int c_dcg_node_append(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition) noexcept nogil
    int c_dcg_node_append_auto(dcg_node* parent, dcg_node* child) noexcept nogil
    int c_dcg_node_append_at(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition, size_t index) noexcept nogil
    int c_dcg_node_append_at_binary(dcg_node* parent, dcg_node* child, c_bool condition, size_t index) noexcept nogil
    int c_dcg_node_detach(dcg_node* node) noexcept nogil
    int c_dcg_node_replace(dcg_node* old_node, dcg_node* new_node) noexcept nogil
    int c_dcg_node_replace_shared(dcg_node* old_node, dcg_node* new_node) noexcept nogil
    dcg_node* c_dcg_node_new_placeholder(allocator_protocol* allocator) noexcept nogil
    size_t c_dcg_node_consolidate_placeholder(dcg_node* node) noexcept nogil
    dcg_node* c_dcg_node_get_placeholder(dcg_node* node) noexcept nogil

    size_t c_dcg_node_child_count(const dcg_node* node) noexcept nogil
    dcg_node* c_dcg_node_first_child(const dcg_node* node) noexcept nogil
    dcg_node* c_dcg_node_last_child(const dcg_node* node) noexcept nogil
    dcg_node* c_dcg_node_child_at(const dcg_node* node, size_t index) noexcept nogil
    ssize_t c_dcg_node_child_index(const dcg_node* node) noexcept nogil
    dcg_node* c_dcg_node_child_by_condition(const dcg_node* node, const dcg_node_edge_condition* condition) noexcept nogil
    dcg_node* c_dcg_node_next_sibling(const dcg_node* node) noexcept nogil
    dcg_node* c_dcg_node_prev_sibling(const dcg_node* node) noexcept nogil
    dcg_node* c_dcg_node_root(dcg_node* node) noexcept nogil
    size_t c_dcg_node_depth(const dcg_node* node) noexcept nogil
    size_t c_dcg_node_height(const dcg_node* node) noexcept nogil
    size_t c_dcg_node_subtree_size(const dcg_node* node) noexcept nogil
    size_t c_dcg_node_leaf_count(const dcg_node* node) noexcept nogil
    c_bool c_dcg_node_is_leaf(const dcg_node* node) noexcept nogil
    c_bool c_dcg_node_is_root(const dcg_node* node) noexcept nogil
    c_bool c_dcg_node_is_ancestor(const dcg_node* node, const dcg_node* descendant) noexcept nogil
    dcg_node* c_dcg_node_find_by_uid(const dcg_node* root, const uuid_t uid) noexcept nogil
    size_t c_dcg_node_collect_leaves(const dcg_node* root, dcg_node** out, size_t cap) noexcept nogil
    size_t c_dcg_node_collect_descendants(const dcg_node* root, dcg_node** out, size_t cap) noexcept nogil
    size_t c_dcg_node_path_to(const dcg_node* node, const dcg_node** out, size_t cap) noexcept nogil

    int c_dcg_node_add_label(dcg_node* node, const char* label) noexcept nogil
    c_bool c_dcg_node_has_label(const dcg_node* node, const char* label) noexcept nogil
    int c_dcg_node_remove_label(dcg_node* node, const char* label) noexcept nogil
    size_t c_dcg_node_label_count(const dcg_node* node) noexcept nogil

    c_bool c_dcg_node_validate(const dcg_node* root, dcg_validate_report* report) noexcept nogil
    int c_dcg_node_validate_print(const dcg_node* root, FILE* stream) noexcept nogil

    void c_dcg_render_opts_default(dcg_render_opts* opts) noexcept nogil
    int c_dcg_node_render(const dcg_node* node, FILE* stream, const dcg_render_opts* opts) noexcept nogil
    int c_dcg_node_print(const dcg_node* node) noexcept nogil
    int c_dcg_node_render_to_string(const dcg_node* node, char* out, size_t cap, const dcg_render_opts* opts) noexcept nogil
    void c_dcg_node_arm_uuid(dcg_node* node) noexcept nogil
    int c_dcg_node_format_uid(const uuid_t uid, char* out, size_t cap) noexcept nogil
    uint64_t c_dcg_node_getpid() noexcept nogil
    void c_dcg_sb_init(dcg_strbuf* buf, char* data, size_t cap) noexcept nogil
    void c_dcg_sb_puts(dcg_strbuf* buf, const char* text) noexcept nogil
    void c_dcg_sb_printf(dcg_strbuf* buf, const char* fmt, ...) noexcept nogil
    const dcg_node_edge_condition* c_dcg_node_infer_condition(const dcg_node* parent) noexcept nogil
    int c_dcg_node_link(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition, dcg_node* anchor) noexcept nogil
    void c_dcg_validate_fail(dcg_validate_report* report, dcg_node* node, dcg_ret_code err, c_bool* valid) noexcept nogil
    c_bool c_dcg_node_validate_walk(const dcg_node* node, dcg_validate_report* report, size_t depth) noexcept nogil
    int c_dcg_node_format_flags(uint32_t flags, char* out, size_t cap) noexcept nogil
    int c_dcg_node_format_line(const dcg_node* node, char* out, size_t cap, const dcg_render_opts* opts) noexcept nogil
    int c_dcg_node_render_walk(const dcg_node* node, FILE* stream, const dcg_render_opts* opts, const char* prefix, c_bool is_last, size_t depth) noexcept nogil
    size_t c_dcg_node_collect_leaves_walk(const dcg_node* node, dcg_node** out, size_t cap, size_t* written) noexcept nogil
    size_t c_dcg_node_collect_descendants_walk(const dcg_node* node, dcg_node** out, size_t cap, size_t* written) noexcept nogil


cdef extern from "decision_graph/decision_tree/bake/c_hierarchy.h":
    void c_dcg_node_free_generic(dcg_node* node) noexcept nogil

    ctypedef struct dcg_node_eval_path:
        dcg_node**   node
        dcg_var_t*   eval_val
        size_t       capacity
        size_t       n_nodes
        dcg_ret_code code
        dcg_node*    leaf
        dcg_node*    failed
        uint64_t     seq_id

    ctypedef struct dcg_root_node:
        dcg_node           base
        dcg_node_eval_path eval_path
        c_bool             inherit_contexts


cdef extern from "decision_graph/decision_tree/bake/c_eval.h":
    ctypedef enum dcg_eval_stage:
        DCG_EVAL_STAGE_NONE
        DCG_EVAL_STAGE_PRE_EVAL
        DCG_EVAL_STAGE_EVAL
        DCG_EVAL_STAGE_POST_EVAL
        DCG_EVAL_STAGE_DONE

    ctypedef struct dcg_eval_run:
        uint64_t     seq_id
        dcg_ret_code code
        dcg_node*    failed
        dcg_node*    leaf
        size_t       visited
        size_t       depth
        c_bool       inplace

    int c_dcg_node_eval(dcg_node* node, c_bool inplace) noexcept nogil
    int c_dcg_node_eval_graph(dcg_node* node, dcg_node_eval_path* path) noexcept nogil
    int c_dcg_root_node_eval(dcg_root_node* root) noexcept nogil

    dcg_node_eval_path* c_dcg_node_eval_path_new(size_t capacity, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_eval_path_free(dcg_node_eval_path* path) noexcept nogil


cdef extern from "decision_graph/decision_tree/bake/c_logic_group.h":
    ctypedef struct dcg_logic_group_manager:
        pass

    int c_dcg_lgm_enter_node(dcg_logic_group_manager* mgr, dcg_node* node) noexcept nogil
    int c_dcg_lgm_exit_node(dcg_logic_group_manager* mgr, dcg_node* node) noexcept nogil


cdef enum dcg_eval_override_flag:
    DCG_EVAL_CDEF_OVERRIDE = 1 << 0  # The Cython hook (c_pre_eval, ...) is overridden.
    DCG_EVAL_PY_OVERRIDE   = 1 << 3  # The Python hook (pre_eval, ...) is overridden.


cdef class LogicNode:
    cdef dcg_node* header
    cdef bint owner
    cdef uintptr_t callback_id
    cdef uint32_t eval_hook_flags
    cdef object c_eval_exception

    cdef readonly LogicNode parent
    cdef readonly dict children
    cdef readonly NodeEdgeCondition condition_to_parent

    @staticmethod
    cdef inline LogicNode c_from_header(dcg_node* header, bint owner=?)

    @staticmethod
    cdef inline dcg_logic_group_manager* c_get_manager()

    @staticmethod
    cdef LogicNode c_dcg_node_reconstruct(dcg_node* header, bint owner=?)

    @staticmethod
    cdef void c_node_callback_event_adaptor(dcg_node_event event, dcg_node* node, dcg_node* subject, uint64_t seq_id, void* user_data) noexcept

    @staticmethod
    cdef inline int c_pre_eval_callback_adaptor(dcg_node* node, void* user_data) noexcept

    @staticmethod
    cdef inline int c_eval_fn_callback_adaptor(dcg_node* node, void* user_data) noexcept

    @staticmethod
    cdef inline int c_post_eval_callback_adaptor(dcg_node* node, void* user_data) noexcept

    cdef inline void c_register_node(self)

    cdef int c_pre_eval_fn(self)

    cdef int c_eval_fn(self)

    cdef int c_post_eval_fn(self)

    cdef void c_bind_eval_callback(self)

    @staticmethod
    cdef str c_eval_stage_names(uint32_t stage)

    cdef void c_check_eval_code(self, int ret_code, dcg_node* subject=?)

    cdef void c_append(self, dcg_node* child, dcg_node_edge_condition* condition)

    cdef void c_replace(self, dcg_node* old_node, dcg_node* new_node)

    cdef void c_detach(self)

    cdef str c_render(self, int max_depth, bint show_labels, bint show_out, str style)

    cdef void c_eval(self)


cdef class PlaceholderNode(LogicNode):
    pass


cdef class LogicNodeRegistry(BoundByteMap):
    cdef void* _ws_key_buf

    @staticmethod
    cdef LogicNodeRegistry c_from_header(bytemap* header, bint owner=?)


cdef dcg_logic_group_manager* C_LGM
cdef LogicNodeRegistry NODE_REGISTRY
