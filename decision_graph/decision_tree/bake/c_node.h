#ifndef C_DCG_BAKE_NODE_H
#define C_DCG_BAKE_NODE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>
#include <cbase/bytemap/c_bytemap.h>
#include <cbase/bytemap/xxh3.h>

#include <decision_graph/decision_tree/bake/c_edge.h>
#include <decision_graph/decision_tree/bake/c_var.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// ========== Constants ==========

/** Capacity of the local buffer used to render a node prefix / uid. */
#ifndef DCG_NODE_STRING_MAXLEN
#define DCG_NODE_STRING_MAXLEN 256
#endif

/** Depth at which the renderer stops descending (0 = unlimited). */
#ifndef DCG_NODE_RENDER_MAX_DEPTH
#define DCG_NODE_RENDER_MAX_DEPTH 0
#endif

/*
 * Display text of the auto-generated no-action. It lives here rather than with
 * the rest of the action family's default reprs because two headers need it and
 * only one of them can see c_action.h: the fill that adds one (c_action.h), and
 * the consolidation that turns an unfilled placeholder into one (this header).
 */
#ifndef DCG_DEF_REPR_NOACTION
#define DCG_DEF_REPR_NOACTION "NoAction"
#endif

/*
 * Display text of the auto-generated placeholder. It lives here rather than with
 * the action family's default reprs because the placeholder is not an action: it
 * is a plain node the builder leaves behind, and both its constructor and the
 * consolidation that retires it are in this header.
 */
#ifndef DCG_DEF_REPR_PLACEHOLDER
#define DCG_DEF_REPR_PLACEHOLDER "Placeholder"
#endif

// ========== Structs ==========

/**
 * @brief A node's identity: 16 raw bytes, laid out as an RFC 4122 UUID.
 *
 * Deliberately a byte array rather than a 128-bit integer. The value is never
 * arithmetic - it is compared bytewise, copied whole, and sliced into the 16
 * bytes a Python `uuid.UUID` is built from - and as a char array it asks the
 * node block for no alignment beyond what its widest integer member already
 * needs.
 */
typedef unsigned char          uuid_t[16];

typedef struct dcg_node        dcg_node;

/*
 * Forward-declared here rather than in the header that defines it: two node
 * families name a logic group - a variable reads one's store, a breakpoint
 * breaks out of one - and this is the one header both of them include. The
 * node layer never dereferences it.
 */
typedef struct dcg_logic_group dcg_logic_group;

/**
 * @brief Type of a node, encoded as family | variant.
 *
 * The high nibble is the family and is what the family predicates test
 * (masked with DCG_NODE_FAMILY_MASK and compared against the family head);
 * the low byte identifies the variant. The "Mask and Generics" members are
 * the family heads - a generic member is legal wherever the family is
 * expected.
 */
typedef enum dcg_node_type {
    // === Input Node ===
    DCG_NODE_INPUT        = 0x0000,  // <- Mask and Generics
    DCG_NODE_TRUE         = 0x0001,  // Boolean true constant.
    DCG_NODE_FALSE        = 0x0002,  // Boolean false constant.
    DCG_NODE_DOUBLE       = 0x0003,  // Double constant.
    DCG_NODE_STRING       = 0x0004,  // String constant (payload borrowed).
    DCG_NODE_INT          = 0x0005,  // Integer constant.
    DCG_NODE_VARIABLE     = 0x0006,  // A live read from a logic group's store.
    // === Operator Node ===
    DCG_NODE_OP           = 0x0100,  // <- Mask and Generics
    DCG_NODE_UNARY        = 0x0101,  // One operand child.
    DCG_NODE_BINARY       = 0x0102,  // Two operand children.
    DCG_NODE_TERNARY      = 0x0103,  // Three operand children (cond, then, else).
    DCG_NODE_CALL         = 0x0104,  // Variadic operator (call-like).
    // 0x0200 - RESERVED. The collection family stood here; a mapping, a
    // sequence and a generator are logic groups, not nodes, and live in
    // c_logic_group.h and c_collections.h.
    // === Action Node ===
    DCG_NODE_ACTION       = 0x0400,  // <- Mask and Generics
    DCG_NODE_NOACTION     = 0x0401,  // Leaf action: do nothing.
    DCG_NODE_LONGACTION   = 0x0402,  // Leaf action: long position signal.
    DCG_NODE_SHORTACTION  = 0x0403,  // Leaf action: short position signal.
    DCG_NODE_CANCELACTION = 0x0404,  // Leaf action: cancel outstanding signal.
    DCG_NODE_CLEARACTION  = 0x0405,  // Leaf action: flatten.
    DCG_NODE_PLACEHOLDER  = 0x0406,  // Auto-generated stand-in, replaced on consolidation.
    // === Special Node ===
    DCG_NODE_SPECIAL      = 0x0800,  // <- Mask and Generics
    DCG_NODE_ROOT         = 0x0801,  // Graph entry point - the only parentless node.
    DCG_NODE_BREAKPOINT   = 0x0802   // Inspection sink - evaluation stops here.
} dcg_node_type;

/**
 * @brief Bit masks over a node type: family nibble and variant number.
 */
typedef enum dcg_node_mask {
    DCG_NODE_FAMILY_MASK  = 0x0F00,  // Extracts the family nibble of a dcg_node_type.
    DCG_NODE_VARIANT_MASK = 0x00FF   // Extracts the variant number of a dcg_node_type.
} dcg_node_mask;

/**
 * @brief Per-node behaviour flags.
 */
typedef enum dcg_node_flag {
    DCG_NODE_FLAG_NONE    = 0,       // No flags.
    DCG_NODE_FLAG_FROZEN  = 1 << 0,  // Baked: structural mutation is refused.
    DCG_NODE_FLAG_VISITED = 1 << 1,  // Traversal scratch; cleared by the walker.
    DCG_NODE_FLAG_PRUNED  = 1 << 2   // Removed by a bake-time optimisation, kept for inspection.
} dcg_node_flag;

/**
 * @brief A node label - one entry of the node's label list.
 */
typedef struct dcg_node_label {
    const char*            label;  // Label text.
    bool                   owned;  // true when the node owns the text and frees it.
    struct dcg_node_label* next;   // Next label in the list.
} dcg_node_label;

/**
 * @brief Flags describing the state of a running evaluation.
 */
typedef enum dcg_eval_flag {
    DCG_EVAL_FLAG_NONE          = 0,       // No flags.
    DCG_EVAL_FLAG_SKIP_CHILDREN = 1 << 0,  // Do not descend below this node.
    DCG_EVAL_FLAG_CACHED        = 1 << 1,  // `out` is already valid; the EVAL hook may be skipped.
    DCG_EVAL_FLAG_BREAKPOINT    = 1 << 2,  // Evaluation is suspended at this node.
    DCG_EVAL_FLAG_TRACE         = 1 << 3   // The node wants its hook calls traced.
} dcg_eval_flag;

/**
 * @brief Which of the three eval hooks of a node.
 */
typedef enum dcg_node_hook_type {
    DCG_HOOK_PRE_EVAL  = 0,  // Runs before the node is evaluated.
    DCG_HOOK_EVAL      = 1,  // Produces the node's value, into node->out.
    DCG_HOOK_POST_EVAL = 2,  // Runs after the value is produced.
    DCG_HOOK_COUNT     = 3   // Number of hook types (array bound).
} dcg_node_hook_type;

/**
 * @brief An eval hook: a strategy the evaluator calls on a node.
 *
 * Unlike the fire-and-forget mutation callbacks, an eval hook produces (or
 * adjusts) a value and can fail, hence the int return code. It needs nothing
 * but its node: the node carries its own context (node->eval_ctx) and its own
 * value slot (node->out), and the opaque data registered with it arrives as
 * `user_data` - the same pointer for all three hooks of that node.
 */
typedef int (*dcg_node_hook_fn)(dcg_node* node, void* user_data);

/**
 * @brief The evaluation context of a node - the host of its callback protocol.
 *
 * Each node carries its own context: the three eval hooks, the opaque data
 * they share, and the state a running evaluation needs. The evaluator reaches
 * all of it through the node, so a hook needs no context parameter.
 */
typedef struct dcg_node_eval_ctx {
    dcg_node_hook_fn pre_eval_fn;   // Runs before the node is evaluated.
    dcg_node_hook_fn eval_fn;       // Produces the node's value, into node->out.
    dcg_node_hook_fn post_eval_fn;  // Runs after the value is produced.
    void*            user_data;     // Opaque data shared by the three hooks.
    void*            run;           // Per-run state owned by the evaluator.
    uint64_t         flags;         // dcg_eval_flag bits.
    size_t           depth;         // Depth of this node at its last visit.
    size_t           visits;        // How many times the node was evaluated.
} dcg_node_eval_ctx;

/**
 * @brief Mutation events observed by the registered callbacks.
 */
typedef enum dcg_node_event {
    DCG_NODE_EVENT_CHILD_ADDED   = 0x0101,  // A child was linked under this node.
    DCG_NODE_EVENT_CHILD_UPDATED = 0x0102,  // A child was linked under this node.
    DCG_NODE_EVENT_CHILD_REMOVED = 0x0103,
    DCG_NODE_EVENT_CHILD_CLEARED = 0x0104,
    DCG_NODE_EVENT_MODIFIED      = 0x0201,  // Metadata changed (repr, labels, hooks).
    DCG_NODE_EVENT_EVALUATED     = 0x0202,
} dcg_node_event;

/**
 * @brief Fire-and-forget notification callback.
 *
 * @param event      What happened.
 * @param self       The node the callback is registered on.
 * @param subject    The other node involved (the child), or NULL.
 * @param seq_id     Sequence id of the mutating caller, for self-suppression.
 * @param user_data  Opaque data passed at registration.
 */
typedef void (*dcg_node_callback_fn)(dcg_node_event event, dcg_node* self, dcg_node* subject, uint64_t seq_id, void* user_data);

/**
 * @brief Linked-list node of a registered mutation callback.
 */
typedef struct dcg_node_callback_ctx {
    dcg_node_callback_fn          fn;         // Callback implementation.
    void*                         user_data;  // Opaque data from registration.
    uintptr_t                     id;         // Opaque id for unregistration.
    struct dcg_node_callback_ctx* next;       // Next registered callback.
} dcg_node_callback_ctx;

/**
 * @brief A node of the decision graph.
 *
 * The node owns its `repr`, its labels and their text, a string `out` and any
 * standalone condition adopted onto it. Everything it owns is allocated as a
 * nested child block, so one c_ap_free_owned() - what c_dcg_node_free() ends
 * with - releases the whole of it without a teardown walk. The five built-in
 * edge conditions are static objects, never blocks, and are left alone.
 *
 * What a node does NOT own is its graph: `children` names blocks of its own
 * type that happen to hang below it. c_dcg_node_free() is therefore a LOCAL
 * operation - it unlinks the node from its parent and lets its children go
 * (they become parentless fragments and stay alive). Tearing a whole graph
 * down is the hierarchy's job: c_dcg_node_teardown_root() walks it and frees
 * every node through the free of its type, leaf first.
 *
 * The payload a family adds after the base header - the operands of an
 * expression, the key of a variable - is released by that family's _free,
 * which is what c_dcg_node_free_generic() reaches for a node in hand as a plain
 * dcg_node*.
 */
typedef struct dcg_node {
    // === Eval Callbacks ===
    dcg_node_eval_ctx              eval_ctx;  // Hooks + run state of this node.
    // === Out Buffer ===
    dcg_var_t                      out;  // Value produced by the last evaluation. // OWNED if a string.
    // === Meta Data ===
    dcg_node_type                  ntype;         // Node type.
    const char*                    repr;          // Display text. // OWNED - a nested copy.
    uuid_t                         uid;           // Stable identity: minted at init, mirrored by the Python layer.
    dcg_node_label*                labels;        // Label list.
    bool                           autogen;       // Generated by the builder rather than written by the user.
    uint32_t                       flags;         // dcg_node_flag bits.
    void*                          user_payload;  // Opaque slot for the binding layer.
    dcg_node_callback_ctx*         callbacks;     // Registered mutation observers (calloc/free).
    // === Hierarchy ===
    const dcg_node_edge_condition* condition_to_parent;  // Edge condition. // OWNED if OWN_CONDITION.
    struct dcg_node*               parent;               // Parent node, NULL for the root.
    struct dcg_node*               children;             // First child, NULL for a leaf.
    struct dcg_node*               next_sibling;         // Next child of the same parent.
    struct dcg_node*               prev_sibling;         // Previous child of the same parent.
} dcg_node;

/**
 * @brief Result of a validation walk.
 */
typedef struct dcg_validate_report {
    dcg_ret_code code;      // First problem found (DCG_OK when the graph is valid).
    dcg_node*    node;      // Node that produced the first problem.
    size_t       errors;    // Total problems.
    size_t       warnings;  // Total warnings (unresolved AUTO on a non-baked graph).
    size_t       nodes;     // Nodes visited.
    size_t       depth;     // Deepest level visited.
} dcg_validate_report;

/**
 * @brief Tree drawing style of the console renderer.
 */
typedef enum dcg_render_style {
    DCG_RENDER_ASCII   = 0,  // +-- / |-- style.
    DCG_RENDER_UNICODE = 1   // ├── / └── style.
} dcg_render_style;

/**
 * @brief Console renderer options.
 *
 * Zero-initialize and call c_dcg_render_opts_default() to get the standard
 * layout, then override the fields you care about.
 */
typedef struct dcg_render_opts {
    dcg_render_style style;           // Drawing style.
    size_t           max_depth;       // Stop below this depth (0 = unlimited).
    bool             show_uid;        // Render each node's uid.
    bool             show_labels;     // Render each node's labels.
    bool             show_condition;  // Render the edge condition.
    bool             show_out;        // Render the node's out value.
    bool             show_flags;      // Render the flag bits.
    bool             show_hooks;      // Render which eval hooks are registered.
    bool             show_children;   // Render the child count.
    bool             show_address;    // Render the node address.
} dcg_render_opts;

/**
 * @brief A bounded string builder over a caller-provided buffer.
 *
 * Appends never overflow: once `used` reaches `cap` the builder keeps
 * counting the characters that WOULD have been written, so the caller can
 * detect truncation (used >= cap) instead of silently losing text.
 */
typedef struct dcg_strbuf {
    char*  data;  // Destination buffer. NOT owned.
    size_t cap;   // Capacity of data.
    size_t used;  // Characters that fit (excluding the terminating NUL).
} dcg_strbuf;

// ========== Forward Declarations ==========

// Utilities
static inline const char*                    c_dcg_node_type_name(dcg_node_type ntype);
static inline bool                           c_dcg_node_type_is_input(dcg_node_type ntype);
static inline bool                           c_dcg_node_type_is_op(dcg_node_type ntype);
static inline bool                           c_dcg_node_type_is_flat(dcg_node_type ntype);
static inline bool                           c_dcg_node_type_is_action(dcg_node_type ntype);
static inline bool                           c_dcg_node_type_is_special(dcg_node_type ntype);
static inline size_t                         c_dcg_node_type_arity(dcg_node_type ntype);
static inline uint64_t                       c_dcg_node_gen_seq_id(const void* ptr);
static inline void                           c_dcg_node_unlink(dcg_node* node);
static inline void                           c_dcg_node_adopt_condition(dcg_node* node, const dcg_node_edge_condition* condition);

// Lifecycle
static inline int                            c_dcg_node_init(dcg_node* node, dcg_node_type ntype, const char* repr);
static inline dcg_node*                      c_dcg_node_new(dcg_node_type ntype, const char* repr, allocator_protocol* allocator);
static inline void                           c_dcg_node_dealloc(dcg_node* node);
static inline void                           c_dcg_node_free(dcg_node* node);

// Node metadata
static inline int                            c_dcg_node_set_repr(dcg_node* node, const char* repr);
static inline int                            c_dcg_node_set_string(dcg_node* node, const char* value);

// Mutation callbacks
static inline int                            c_dcg_node_register_callback(dcg_node* node, dcg_node_callback_fn fn, void* user_data, uintptr_t* out_id);
static inline int                            c_dcg_node_unregister_callback(dcg_node* node, uintptr_t callback_id);
static inline void                           c_dcg_node_invoke_callbacks(dcg_node* node, dcg_node_event event, dcg_node* subject, uint64_t seq_id);
static inline size_t                         c_dcg_node_callback_count(const dcg_node* node);

// Graph building
static inline int                            c_dcg_node_append(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition);
static inline int                            c_dcg_node_append_auto(dcg_node* parent, dcg_node* child);
static inline int                            c_dcg_node_append_at(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition, size_t index);
static inline int                            c_dcg_node_append_at_binary(dcg_node* parent, dcg_node* child, bool condition, size_t index);
static inline int                            c_dcg_node_detach(dcg_node* node);
static inline int                            c_dcg_node_replace(dcg_node* old_node, dcg_node* new_node);
static inline int                            c_dcg_node_replace_shared(dcg_node* old_node, dcg_node* new_node);
static inline dcg_node*                      c_dcg_node_new_placeholder(allocator_protocol* allocator);
static inline size_t                         c_dcg_node_consolidate_placeholder(dcg_node* node);
static inline dcg_node*                      c_dcg_node_get_placeholder(dcg_node* node);

// Queries
static inline size_t                         c_dcg_node_child_count(const dcg_node* node);
static inline dcg_node*                      c_dcg_node_first_child(const dcg_node* node);
static inline dcg_node*                      c_dcg_node_last_child(const dcg_node* node);
static inline dcg_node*                      c_dcg_node_child_at(const dcg_node* node, size_t index);
static inline ssize_t                        c_dcg_node_child_index(const dcg_node* node);
static inline dcg_node*                      c_dcg_node_child_by_condition(const dcg_node* node, const dcg_node_edge_condition* condition);
static inline dcg_node*                      c_dcg_node_next_sibling(const dcg_node* node);
static inline dcg_node*                      c_dcg_node_prev_sibling(const dcg_node* node);
static inline dcg_node*                      c_dcg_node_root(dcg_node* node);
static inline size_t                         c_dcg_node_depth(const dcg_node* node);
static inline size_t                         c_dcg_node_height(const dcg_node* node);
static inline size_t                         c_dcg_node_subtree_size(const dcg_node* node);
static inline size_t                         c_dcg_node_leaf_count(const dcg_node* node);
static inline bool                           c_dcg_node_is_leaf(const dcg_node* node);
static inline bool                           c_dcg_node_is_root(const dcg_node* node);
static inline bool                           c_dcg_node_is_ancestor(const dcg_node* node, const dcg_node* descendant);
static inline dcg_node*                      c_dcg_node_find_by_uid(const dcg_node* root, const uuid_t uid);
static inline size_t                         c_dcg_node_collect_leaves(const dcg_node* root, dcg_node** out, size_t cap);
static inline size_t                         c_dcg_node_collect_descendants(const dcg_node* root, dcg_node** out, size_t cap);
static inline size_t                         c_dcg_node_path_to(const dcg_node* node, const dcg_node** out, size_t cap);

// Labels
static inline int                            c_dcg_node_add_label(dcg_node* node, const char* label);
static inline bool                           c_dcg_node_has_label(const dcg_node* node, const char* label);
static inline int                            c_dcg_node_remove_label(dcg_node* node, const char* label);
static inline size_t                         c_dcg_node_label_count(const dcg_node* node);

// Validation
static inline bool                           c_dcg_node_validate(const dcg_node* root, dcg_validate_report* report);
static inline int                            c_dcg_node_validate_print(const dcg_node* root, FILE* stream);

// Rendering
static inline void                           c_dcg_render_opts_default(dcg_render_opts* opts);
static inline int                            c_dcg_node_render(const dcg_node* node, FILE* stream, const dcg_render_opts* opts);
static inline int                            c_dcg_node_print(const dcg_node* node);
static inline int                            c_dcg_node_render_to_string(const dcg_node* node, char* out, size_t cap, const dcg_render_opts* opts);

// Identity
static inline void                           c_dcg_node_arm_uuid(dcg_node* node);
static inline int                            c_dcg_node_format_uid(const uuid_t uid, char* out, size_t cap);

// Internal helpers (exposed for reuse and testing - not part of the stable surface)
static inline uint64_t                       c_dcg_node_getpid(void);
static inline void                           c_dcg_sb_init(dcg_strbuf* buf, char* data, size_t cap);
static inline void                           c_dcg_sb_puts(dcg_strbuf* buf, const char* text);
static inline void                           c_dcg_sb_printf(dcg_strbuf* buf, const char* fmt, ...);
static inline const dcg_node_edge_condition* c_dcg_node_infer_condition(const dcg_node* parent);
static inline int                            c_dcg_node_link(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition, dcg_node* anchor);
static inline void                           c_dcg_validate_fail(dcg_validate_report* report, dcg_node* node, dcg_ret_code err, bool* valid);
static inline bool                           c_dcg_node_validate_walk(const dcg_node* node, dcg_validate_report* report, size_t depth);
static inline int                            c_dcg_node_format_flags(uint32_t flags, char* out, size_t cap);
static inline int                            c_dcg_node_format_line(const dcg_node* node, char* out, size_t cap, const dcg_render_opts* opts);
static inline int                            c_dcg_node_render_walk(const dcg_node* node, FILE* stream, const dcg_render_opts* opts, const char* prefix, bool is_last, size_t depth);
static inline size_t                         c_dcg_node_collect_leaves_walk(const dcg_node* node, dcg_node** out, size_t cap, size_t* written);
static inline size_t                         c_dcg_node_collect_descendants_walk(const dcg_node* node, dcg_node** out, size_t cap, size_t* written);

// ========== Utility Functions ==========

/*
 * One name table per family, indexed by the variant number (the low byte of
 * the type). Adding a variant means adding a name in the right table and
 * nothing else - the family is selected by the mask, never by parsing.
 */
static const char* const                     DCG_NODE_INPUT_NAMES[]   = {"INPUT", "TRUE", "FALSE", "DOUBLE", "STRING", "INT", "VARIABLE"};
static const char* const                     DCG_NODE_OP_NAMES[]      = {"OP", "UNARY", "BINARY", "TERNARY", "CALL"};
static const char* const                     DCG_NODE_ACTION_NAMES[]  = {"ACTION", "NOACTION", "LONGACTION", "SHORTACTION", "CANCELACTION", "CLEARACTION", "PLACEHOLDER"};
static const char* const                     DCG_NODE_SPECIAL_NAMES[] = {"SPECIAL", "ROOT", "BREAKPOINT"};

/**
 * @brief Look a variant number up in its family's name table.
 *
 * @param names    Name table of the family.
 * @param count    Entries in the table.
 * @param variant  Variant number (the low byte of the type).
 * @return The name, or "UNKNOWN" past the end of the table.
 */
static inline const char*                    c_dcg_name_at(const char* const* names, size_t count, size_t variant) {
    if (!names || variant >= count) return "UNKNOWN";
    return names[variant];
}

/**
 * @brief Stable display name of a node type (the enum name without its prefix).
 *
 * @param ntype  Node type.
 * @return Static string; "UNKNOWN" for an out-of-range type.
 */
static inline const char* c_dcg_node_type_name(dcg_node_type ntype) {
    size_t variant = (size_t) ((int) ntype & DCG_NODE_VARIANT_MASK);

    switch ((int) ntype & DCG_NODE_FAMILY_MASK) {
        case DCG_NODE_INPUT:
            return c_dcg_name_at(DCG_NODE_INPUT_NAMES, sizeof(DCG_NODE_INPUT_NAMES) / sizeof(char*), variant);
        case DCG_NODE_OP:
            return c_dcg_name_at(DCG_NODE_OP_NAMES, sizeof(DCG_NODE_OP_NAMES) / sizeof(char*), variant);
        case DCG_NODE_ACTION:
            return c_dcg_name_at(DCG_NODE_ACTION_NAMES, sizeof(DCG_NODE_ACTION_NAMES) / sizeof(char*), variant);
        case DCG_NODE_SPECIAL:
            return c_dcg_name_at(DCG_NODE_SPECIAL_NAMES, sizeof(DCG_NODE_SPECIAL_NAMES) / sizeof(char*), variant);
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Predicate: is the type in the input family?
 *
 * The input family is what a graph is fed: a literal it carries, or a read it
 * takes from the store of a logic group.
 *
 * @param ntype  Node type.
 * @return true for input types (the family head included).
 */
static inline bool c_dcg_node_type_is_input(dcg_node_type ntype) {
    return (ntype & DCG_NODE_FAMILY_MASK) == DCG_NODE_INPUT;
}

/**
 * @brief Predicate: is the type in the operator family?
 *
 * @param ntype  Node type.
 * @return true for operator types.
 */
static inline bool c_dcg_node_type_is_op(dcg_node_type ntype) {
    return (ntype & DCG_NODE_FAMILY_MASK) == DCG_NODE_OP;
}

/**
 * @brief Predicate: does the type live in the base header alone?
 *
 * A literal IS the whole struct - its variant adds no field of its own - so a
 * dcg_node block holds it. An expression carries its operand array after the
 * header, a variable its key and its group, an action its placement fields, a
 * root or a breakpoint its walk state: those need their family's constructor,
 * because only the family knows how big the block has to be.
 *
 * @param ntype  Node type.
 * @return true when a plain dcg_node block is enough for the type.
 */
static inline bool c_dcg_node_type_is_flat(dcg_node_type ntype) {
    // A placeholder is the one action-family type that carries nothing beyond
    // the base node - no signal, no payload, no connect flag - so its block is
    // one dcg_node like an input's, and the base constructor can build it. It
    // is asked first because the family switch below reads "input, and nothing
    // else", which would otherwise refuse it.
    if (ntype == DCG_NODE_PLACEHOLDER) return true;

    switch ((int) ntype & DCG_NODE_FAMILY_MASK) {
        case DCG_NODE_INPUT:
            return ntype != DCG_NODE_VARIABLE;  // a variable carries its key and its group
        default:
            return false;  // an operator carries operands, the rest carry their own state
    }
}

/**
 * @brief Predicate: is the type in the action family?
 *
 * Action nodes are leaves: the graph terminates (or hands over control) at them.
 *
 * @param ntype  Node type.
 * @return true for action types.
 */
static inline bool c_dcg_node_type_is_action(dcg_node_type ntype) {
    return (ntype & DCG_NODE_FAMILY_MASK) == DCG_NODE_ACTION;
}

/**
 * @brief Predicate: is the type in the special family (root / breakpoint)?
 *
 * @param ntype  Node type.
 * @return true for special types.
 */
static inline bool c_dcg_node_type_is_special(dcg_node_type ntype) {
    return (ntype & DCG_NODE_FAMILY_MASK) == DCG_NODE_SPECIAL;
}

/**
 * @brief Fixed operand count of a node type.
 *
 * @param ntype  Node type.
 * @return 1 / 2 / 3 for the fixed-arity operators; 0 for everything else
 *         (inputs, actions, and the variadic CALL).
 */
static inline size_t c_dcg_node_type_arity(dcg_node_type ntype) {
    switch (ntype) {
        case DCG_NODE_UNARY:
            return 1;
        case DCG_NODE_BINARY:
            return 2;
        case DCG_NODE_TERNARY:
            return 3;
        default:
            return 0;
    }
}

/**
 * @brief Process id, used as the seq_id seed (portable across POSIX and Windows).
 *
 * @return The current process id.
 */
static inline uint64_t c_dcg_node_getpid(void) {
#ifdef _WIN32
    return (uint64_t) _getpid();
#else
    return (uint64_t) getpid();
#endif
}

/**
 * @brief Generate a sequence id for a node - the project-standard pointer/PID mix.
 *
 * A seq_id lets a callback recognise the mutation it caused itself
 * (self-suppression): the Python wrapper regenerates its id whenever it is
 * (re)bound, so a callback that sees `seq_id == wrapper->seq_id` knows the
 * wrapper already applied the change locally and can return early.
 *
 * @param ptr  Address to derive the id from - the node block start.
 * @return The sequence id (0 when ptr is NULL).
 */
static inline uint64_t c_dcg_node_gen_seq_id(const void* ptr) {
    if (!ptr) return 0;
    uint64_t addr = (uint64_t) (uintptr_t) ptr;
    return XXH3_64bits_withSeed(&addr, sizeof(addr), c_dcg_node_getpid());
}

/**
 * @brief Mint a node's identity from what makes it distinct, and arm the field.
 *
 * Hashed, not counted: a process-unique id needs no registry to hand out and no
 * allocation to store, and the three inputs below are exactly what tells one
 * node from another - the block it lives in, the type it is, and the text a
 * reader of the graph knows it by. `c_dcg_node_init` calls this once the node
 * is whole (type, out and repr all in place); calling it again later re-mints.
 *
 * The last two assignments lay the digest out as a valid RFC 4122 UUID -
 * version 4, variant 1 - so the 16 bytes round-trip through `uuid.UUID` at the
 * Python boundary without further massaging.
 *
 * @param node  Node to arm (NULL-safe).
 */
static inline void c_dcg_node_arm_uuid(dcg_node* node) {
    if (!node) return;

    /* Stack state, no allocation. */
    XXH3_state_t st;
    XXH3_128bits_reset(&st);

    /* 1. block identity - unique within the process while the node lives */
    uintptr_t addr = (uintptr_t) node;
    XXH3_128bits_update(&st, &addr, sizeof(addr));

    /* 2. the type, so a recycled block never inherits its predecessor's id */
    XXH3_128bits_update(&st, &node->ntype, sizeof(node->ntype));

    /* 3. the repr, which is what the tree is read by */
    if (node->repr) XXH3_128bits_update(&st, node->repr, strlen(node->repr));

    XXH128_hash_t h = XXH3_128bits_digest(&st);
    memcpy(node->uid, &h.low64, sizeof(h.low64));
    memcpy(node->uid + sizeof(h.low64), &h.high64, sizeof(h.high64));

    /* RFC 4122 layout: version 4, variant 1. */
    node->uid[6] = (node->uid[6] & 0x0F) | 0x40;
    node->uid[8] = (node->uid[8] & 0x3F) | 0x80;
}

/**
 * @brief Bind a string builder to a caller-provided buffer.
 *
 * @param buf   Builder to initialize.
 * @param data  Destination buffer.
 * @param cap   Capacity of data.
 */
static inline void c_dcg_sb_init(dcg_strbuf* buf, char* data, size_t cap) {
    if (!buf) return;
    buf->data = data;
    buf->cap  = cap;
    buf->used = 0;
    if (data && cap > 0) data[0] = '\0';
}

/**
 * @brief Append a string to a builder.
 *
 * @param buf   Builder (NULL-safe).
 * @param text  Text to append (NULL is skipped).
 */
static inline void c_dcg_sb_puts(dcg_strbuf* buf, const char* text) {
    if (!buf || !text) return;
    size_t len = strlen(text);
    if (buf->used + len < buf->cap) {
        memcpy(buf->data + buf->used, text, len);
        buf->data[buf->used + len] = '\0';
    }
    buf->used += len;
}

/**
 * @brief Append formatted text to a builder.
 *
 * @param buf  Builder (NULL-safe).
 * @param fmt  printf format.
 * @param ...  Format arguments.
 */
static inline void c_dcg_sb_printf(dcg_strbuf* buf, const char* fmt, ...) {
    if (!buf || !fmt) return;

    char    scratch[DCG_NODE_STRING_MAXLEN];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(scratch, sizeof(scratch), fmt, args);
    va_end(args);

    if (n < 0) return;
    if ((size_t) n < sizeof(scratch)) {
        c_dcg_sb_puts(buf, scratch);
        return;
    }

    // Long payload: re-format straight into the destination buffer.
    if (buf->used >= buf->cap) {
        buf->used += (size_t) n;
        return;
    }
    size_t room = buf->cap - buf->used;
    va_start(args, fmt);
    int written = vsnprintf(buf->data + buf->used, room, fmt, args);
    va_end(args);
    if (written >= 0 && (size_t) written < room) buf->used += (size_t) written;
    else buf->used += (size_t) n;
}

/**
 * @brief Unlink a node from its parent's child list, without any policy check.
 *
 * Shared by the detach API and by teardown: a node being torn down must
 * leave the parent's list even when the parent is FROZEN, otherwise the
 * parent would keep a dangling pointer.
 *
 * @param node  Node to unlink (NULL-safe: a parentless node is a no-op).
 */
static inline void c_dcg_node_unlink(dcg_node* node) {
    if (!node || !node->parent) return;

    dcg_node* parent = node->parent;
    if (node->prev_sibling) node->prev_sibling->next_sibling = node->next_sibling;
    else parent->children = node->next_sibling;
    if (node->next_sibling) node->next_sibling->prev_sibling = node->prev_sibling;

    node->parent              = NULL;
    node->next_sibling        = NULL;
    node->prev_sibling        = NULL;
    node->condition_to_parent = DCG_NO_CONDITION;
}

/**
 * @brief Adopt an edge condition onto a node.
 *
 * A node owns the condition of its edge: the condition becomes a child block
 * of the node, so it is released with the node and needs no teardown step of
 * its own. The five built-ins are static objects, never blocks, and are left
 * alone - which is the one case worth distinguishing, and it is a plain
 * pointer comparison.
 *
 * Everything else is expected to be an allocator-protocol block, which is what
 * c_dcg_edge_new() hands out. Sniffing for that is deliberately NOT done here:
 * asking whether a pointer is a block means reading the bytes before it, which
 * is out of bounds for anything that is not one.
 *
 * @param node       Node to adopt onto (NULL-safe).
 * @param condition  Condition to adopt (NULL-safe).
 */
static inline void c_dcg_node_adopt_condition(dcg_node* node, const dcg_node_edge_condition* condition) {
    if (!node || !condition) return;
    if (c_dcg_condition_is_sentinel(condition)) return;  // a static built-in, never a block
    c_ap_acquire_ownership((void*) condition, node);     // re-parent it under the node
}

// ========== Lifecycle Methods ==========

/**
 * @brief Initialize a node buf, taking a copy of its display text.
 *
 * The buf is populated from scratch (zeroing it first, so a reused buf is
 * safe). The repr is always copied into a block owned by the node - a node
 * never borrows display text - and both the node block and that copy come from
 * the allocator protocol: a node is always an allocator block, never an
 * embedded field, which is what lets it own everything it carries.
 *
 * @param node   Node to initialize.
 * @param ntype  Node type.
 * @param repr   Display text to copy (may be NULL).
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_INVALID_BUF or DCG_ERR_OOM.
 */
static inline int c_dcg_node_init(dcg_node* node, dcg_node_type ntype, const char* repr) {
    if (!node) return DCG_ERR_INVALID_ARG;

    memset(node, 0, sizeof(*node));
    node->ntype = ntype;
    (void) c_dcg_var_init(&node->out);

    int ret_code = c_dcg_node_set_repr(node, repr);
    if (ret_code != DCG_OK) return ret_code;

    /* Identity last: the digest reads the type and the repr the lines above set. */
    c_dcg_node_arm_uuid(node);
    return DCG_OK;
}

/**
 * @brief Give a node its own copy of a display text.
 *
 * The copy is nested under the node and replaces (and releases) whatever repr
 * the node held before, so a node's repr is always owned and always dies with
 * the node.
 *
 * @param node  Node to modify.
 * @param repr  Text to copy (NULL clears the repr).
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_INVALID_BUF or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_repr(dcg_node* node, const char* repr) {
    if (!node) return DCG_ERR_INVALID_ARG;

    char* copy = NULL;
    if (repr) {
        size_t len = strlen(repr);
        copy       = (char*) c_ap_alloc_child(len + 1, NULL, node);
        if (!copy) return DCG_ERR_OOM;
        memcpy(copy, repr, len + 1);
    }

    if (node->repr) c_ap_free_owned((void*) node->repr);  // the copy we are replacing
    node->repr = copy;
    return DCG_OK;
}

/**
 * @brief Allocate and initialize a node.
 *
 * @param ntype      Node type.
 * @param repr       Borrowed display text (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_node* c_dcg_node_new(dcg_node_type ntype, const char* repr, allocator_protocol* allocator) {
    /* The block is one dcg_node, so the types that carry more are refused here
     * and built by the constructor named after their family - see c_const.h,
     * c_expr.h and c_action.h. */
    if (!c_dcg_node_type_is_flat(ntype)) return NULL;

    dcg_node* node = (dcg_node*) c_ap_alloc(sizeof(dcg_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(node, ntype, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

/**
 * @brief Tear down a node - leaves a zeroed buf.
 *
 * Releases what the BASE owns: the labels and their text, the callback
 * contexts, the repr, a string out and an adopted condition. The node is then
 * unlinked from its parent and its children are let go - each one is unlinked
 * (c_dcg_node_unlink), so it survives as a parentless fragment. A node owns
 * its block, not its graph: the subtree below it is c_dcg_node_teardown_root's
 * business, and the payload a family adds after the header is that family's
 * own _free (see c_dcg_node_free_generic).
 *
 * The node's own buf is NOT freed - that is _free's job. A borrowed repr is
 * left untouched; a built-in condition is a static object, never a block.
 *
 * @param node  Node to tear down (NULL-safe).
 */
static inline void c_dcg_node_dealloc(dcg_node* node) {
    if (!node) return;

    // An adopted condition is a nested block of this node. It goes before the
    // unlink, which drops the pointer to it.
    if (node->condition_to_parent && !c_dcg_condition_is_sentinel(node->condition_to_parent)) {
        c_ap_free_owned((void*) node->condition_to_parent);
    }

    // Unlink from the parent first: a teardown must never leave a dangling
    // link behind in the parent's child list. This deliberately bypasses the
    // FROZEN guard - a frozen parent must not be able to pin a dead child.
    dcg_node* parent = node->parent;
    c_dcg_node_unlink(node);
    if (parent) c_dcg_node_invoke_callbacks(parent, DCG_NODE_EVENT_CHILD_REMOVED, node, (uint64_t) -1);

    // The children are blocks of their own: they are let go, not freed, and
    // each keeps its own subtree. Unlinking pops one child per round, so the
    // list we are walking is the one being emptied.
    while (node->children) c_dcg_node_unlink(node->children);

    // Labels are nested blocks, and each entry's text is nested under the
    // entry: one free per entry releases the pair.
    dcg_node_label* label = node->labels;
    node->labels          = NULL;
    while (label) {
        dcg_node_label* next = label->next;
        c_ap_free_owned(label);
        label = next;
    }

    // Callback contexts are short-lived registration state: calloc / free.
    dcg_node_callback_ctx* cb = node->callbacks;
    while (cb) {
        dcg_node_callback_ctx* next = cb->next;
        free(cb);
        cb = next;
    }

    // The repr and a string out are nested blocks, released here so the block
    // owns nothing once the teardown is done.
    if (node->repr) c_ap_free_owned((void*) node->repr);
    if (node->out.dtype == VAR_TYPE_STRING && node->out.value.as_string) {
        c_ap_free_owned((void*) node->out.value.as_string);
    }

    memset(node, 0, sizeof(*node));
}

/**
 * @brief Tear down a node and free its buf - the node itself, nothing below it.
 *
 * The children are unlinked and left alive (see c_dcg_node_dealloc), so this
 * is the free of a node, not of a subtree. Free a whole graph from its top
 * with c_dcg_node_teardown_root() - never by calling this on a branch.
 *
 * @param node  Node to free (NULL-safe): a base node, or one of the types the
 *              base alone holds. A family that adds state after the header -
 *              an expression, a variable, a root, a breakpoint - is
 *              torn down by its own _free, or by c_dcg_node_free_generic() when
 *              only its dcg_node* is in hand.
 */
static inline void c_dcg_node_free(dcg_node* node) {
    if (!node) return;
    c_dcg_node_dealloc(node);
    c_ap_free_owned(node);  // everything the node nested is released here
}

// ========== Public APIs - Typed Constructors ==========

/*
 * The payload-free types. Everything with state of its own - literals,
 * expressions, variables - is constructed by its own header.
 */

/**
 * @brief Give a node an owned copy of a string value.
 *
 * The copy is nested under the node, so it dies with the node; a previous
 * string value of the node is released first.
 *
 * @param node   Node to modify.
 * @param value  Text to copy (NULL stores an empty value).
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_set_string(dcg_node* node, const char* value) {
    if (!node) return DCG_ERR_INVALID_ARG;

    if (node->out.dtype == VAR_TYPE_STRING && node->out.value.as_string) {
        c_ap_free_owned((void*) node->out.value.as_string);
        (void) c_dcg_var_init(&node->out);
    }
    if (!value) return c_dcg_var_init_string(&node->out, NULL);

    size_t len  = strlen(value);
    char*  copy = (char*) c_ap_alloc_child(len + 1, NULL, node);
    if (!copy) return DCG_ERR_OOM;
    memcpy(copy, value, len + 1);

    return c_dcg_var_init_string(&node->out, copy);
}

// ========== Eval Hooks ==========

/**
 * @brief Register (or replace) one eval hook of a node.
 *
 * The three hooks of a node share one `user_data`: it is written on every
 * registration, so the last registration's pointer is the one all three
 * receive.
 *
 * @param node       Node to attach the hook to.
 * @param user_data  Opaque data handed back to the callback.
 * @param out_id     Receives the id for unregistration (may be NULL).
 * @return DCG_OK, or DCG_ERR_INVALID_ARG / DCG_ERR_OOM.
 */
static inline int c_dcg_node_register_callback(dcg_node* node, dcg_node_callback_fn fn, void* user_data, uintptr_t* out_id) {
    if (!node || !fn) return DCG_ERR_INVALID_ARG;

    dcg_node_callback_ctx* cb = (dcg_node_callback_ctx*) calloc(1, sizeof(dcg_node_callback_ctx));
    if (!cb) return DCG_ERR_OOM;

    cb->fn        = fn;
    cb->user_data = user_data;
    cb->id        = (uintptr_t) cb;

    if (!node->callbacks) {
        node->callbacks = cb;
    }
    else {
        dcg_node_callback_ctx* tail = node->callbacks;
        while (tail->next) tail = tail->next;
        tail->next = cb;
    }

    if (out_id) *out_id = cb->id;
    return DCG_OK;
}

/**
 * @brief Unregister a mutation observer by its id.
 *
 * @param node         Node to modify.
 * @param callback_id  Id returned by c_dcg_node_register_callback.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_NOT_FOUND.
 */
static inline int c_dcg_node_unregister_callback(dcg_node* node, uintptr_t callback_id) {
    if (!node) return DCG_ERR_INVALID_ARG;

    dcg_node_callback_ctx* prev = NULL;
    dcg_node_callback_ctx* curr = node->callbacks;
    while (curr) {
        if (curr->id == callback_id) {
            if (prev) prev->next = curr->next;
            else node->callbacks = curr->next;
            free(curr);
            return DCG_OK;
        }
        prev = curr;
        curr = curr->next;
    }
    return DCG_ERR_NOT_FOUND;
}

/**
 * @brief Notify every registered observer of a mutation.
 *
 * The next pointer is snapshotted before each call, so a callback may
 * unregister itself (or another callback) while being invoked.
 *
 * @param node     Node whose observers to notify (NULL-safe).
 * @param event    What happened.
 * @param subject  The other node involved (may be NULL).
 * @param seq_id   Id of the mutating caller; pass (uint64_t) -1 for
 *                 lifecycle events that have no self-suppression semantics.
 */
static inline void c_dcg_node_invoke_callbacks(dcg_node* node, dcg_node_event event, dcg_node* subject, uint64_t seq_id) {
    if (!node || !node->callbacks) return;

    dcg_node_callback_ctx* cb = node->callbacks;
    while (cb) {
        dcg_node_callback_ctx* next = cb->next;
        if (cb->fn) cb->fn(event, node, subject, seq_id, cb->user_data);
        cb = next;
    }
}

/**
 * @brief How many mutation observers are registered on a node.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The observer count.
 */
static inline size_t c_dcg_node_callback_count(const dcg_node* node) {
    if (!node) return 0;
    size_t                 count = 0;
    dcg_node_callback_ctx* cb    = node->callbacks;
    while (cb) {
        count++;
        cb = cb->next;
    }
    return count;
}

// ========== Graph Building ==========

/**
 * @brief Infer the edge condition of a child that is about to be appended.
 *
 * This is the bake port of the capi's c_infer_condition(): the rule reads
 * the parent's EXISTING branches - the node's dtype plays no part in it.
 *
 *   - no children yet       -> TRUE (the first branch is always TRUE)
 *   - one binary child      -> the opposite binary
 *   - one else child        -> TRUE, when that child is auto-generated
 *   - one other child       -> unresolved
 *   - two or more children  -> the newest auto-generated child's condition,
 *                              so the builder can find and replace the edge
 *                              it created itself; otherwise unresolved
 *
 * @param parent  Parent the edge will be attached to (NULL-safe).
 * @return The inferred condition, or NULL when nothing can be inferred.
 */
static inline const dcg_node_edge_condition* c_dcg_node_infer_condition(const dcg_node* parent) {
    size_t    count = c_dcg_node_child_count(parent);
    dcg_node* last  = c_dcg_node_last_child(parent);

    if (count == 0) return DCG_TRUE_CONDITION;

    if (count == 1 && last) {
        const dcg_node_edge_condition* last_condition = last->condition_to_parent;
        if (c_dcg_condition_is_binary(last_condition)) {
            return c_dcg_condition_is_true(last_condition) ? DCG_FALSE_CONDITION : DCG_TRUE_CONDITION;
        }
        if (c_dcg_condition_is_else(last_condition) && last->autogen) return DCG_TRUE_CONDITION;
        return NULL;
    }

    if (last && last->autogen) return last->condition_to_parent;
    dcg_node* second = last ? last->prev_sibling : NULL;
    if (second && second->autogen) return second->condition_to_parent;
    return NULL;
}

/**
 * @brief Link a child into a parent's child list, applying every edge rule.
 *
 * Shared by c_dcg_node_append() and c_dcg_node_append_at(). The condition
 * is resolved (AUTO inference), the root's unary/unconditioned rule is
 * enforced, and one edge per condition is guaranteed - the capi keys its
 * children dict by condition, so a duplicate edge is a build error there
 * and here alike.
 *
 * @param parent     Parent to link into.
 * @param child      Child to link (must be parentless).
 * @param condition  Requested edge condition (never NULL).
 * @param anchor     Insert before this child, or NULL to append at the tail.
 * @return DCG_OK or a DCG_ERR_* code.
 */
static inline int c_dcg_node_link(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition, dcg_node* anchor) {
    if (parent == child) return DCG_ERR_CYCLE;
    if (child->parent) return DCG_ERR_BUSY;
    if (parent->flags & DCG_NODE_FLAG_FROZEN) return DCG_ERR_BUSY;
    if (c_dcg_node_is_ancestor(child, parent)) return DCG_ERR_CYCLE;

    if (parent->ntype == DCG_NODE_ROOT) {
        // A root is strictly unary (one entry point) and strictly unconditioned.
        if (parent->children) return DCG_ERR_TYPE;
        if (!c_dcg_condition_is_none(condition) && !c_dcg_condition_is_auto(condition)) return DCG_ERR_EDGE;
        condition = DCG_NO_CONDITION;
    }
    else if (c_dcg_condition_is_auto(condition)) {
        condition = c_dcg_node_infer_condition(parent);
        if (!condition) return DCG_ERR_UNRESOLVED;
    }

    if (c_dcg_node_child_by_condition(parent, condition)) return DCG_ERR_DUPLICATE;

    // An else branch is the fallback, so it is always the last child: nothing
    // may be appended behind it, and it can never be inserted in front of
    // another child. Inserting a regular branch somewhere before a trailing
    // else is fine - the else stays last.
    if (!anchor && c_dcg_node_child_by_condition(parent, DCG_ELSE_CONDITION)) return DCG_ERR_EDGE;
    if (anchor && c_dcg_condition_is_else(condition)) return DCG_ERR_EDGE;

    child->parent              = parent;
    child->condition_to_parent = condition;
    c_dcg_node_adopt_condition(child, condition);  // a standalone condition dies with its child

    if (anchor) {
        child->next_sibling = anchor;
        child->prev_sibling = anchor->prev_sibling;
        if (child->prev_sibling) child->prev_sibling->next_sibling = child;
        else parent->children = child;
        anchor->prev_sibling = child;
    }
    else {
        child->prev_sibling = c_dcg_node_last_child(parent);
        child->next_sibling = NULL;
        if (child->prev_sibling) child->prev_sibling->next_sibling = child;
        else parent->children = child;
    }

    c_dcg_node_invoke_callbacks(parent, DCG_NODE_EVENT_CHILD_ADDED, child, (uint64_t) -1);
    return DCG_OK;
}

/**
 * @brief Append a child at the tail of a node's child list.
 *
 * DCG_AUTO_CONDITION is resolved by inference over the parent's existing
 * branches (see c_dcg_node_infer_condition) before it is stored; every
 * other condition is used verbatim. A NULL condition is rejected - the
 * capi makes the same call, refusing to guess an edge.
 *
 * Rejected: a NULL argument, a child that already has a parent (detach it
 * first), a child that is the parent or one of its ancestors (a cycle), a
 * condition already registered on this parent, an else branch registered
 * anywhere but last, a root that already has its entry child, a condition
 * a root may not carry, an edge that cannot be inferred, and any mutation
 * of a FROZEN (baked) node.
 *
 * @param parent     Node to append to.
 * @param child      Node to append (must be currently parentless).
 * @param condition  Edge condition, never NULL. A standalone (allocator
 *                   block) condition is adopted by the child and dies with
 *                   it; the built-ins and borrowed conditions are not.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG / BUSY / CYCLE / DUPLICATE / EDGE /
 *         TYPE / UNRESOLVED.
 */
static inline int c_dcg_node_append(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition) {
    if (!parent || !child || !condition) return DCG_ERR_INVALID_ARG;
    return c_dcg_node_link(parent, child, condition, NULL);
}

/**
 * @brief Append a child, letting the parent infer the branch condition.
 *
 * Equivalent to c_dcg_node_append(parent, child, NULL) for a non-boolean
 * parent and to the AUTO inference for a boolean one.
 *
 * @param parent  Node to append to.
 * @param child   Node to append (must be currently parentless).
 * @return DCG_OK or a DCG_ERR_* code.
 */
static inline int c_dcg_node_append_auto(dcg_node* parent, dcg_node* child) {
    return c_dcg_node_append(parent, child, DCG_AUTO_CONDITION);
}

/**
 * @brief Insert a child at a given position of a node's child list.
 *
 * Applies the same rules as c_dcg_node_append and inserts at `index`
 * instead of the tail; an index at or past the end appends. The else
 * ordering rule makes an insert behind an else branch impossible, and an
 * else branch itself can only ever be appended at the tail.
 *
 * @param parent     Node to insert into.
 * @param child      Node to insert (must be currently parentless).
 * @param condition  Edge condition, never NULL.
 * @param index      Position to insert at; >= child_count appends.
 * @return DCG_OK or a DCG_ERR_* code.
 */
static inline int c_dcg_node_append_at(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition, size_t index) {
    if (!parent || !child || !condition) return DCG_ERR_INVALID_ARG;
    return c_dcg_node_link(parent, child, condition, c_dcg_node_child_at(parent, index));
}

/**
 * @brief Insert a child at a position, with a boolean branch edge.
 *
 * The convenience form of c_dcg_node_append_at() for the common two-way case:
 * the condition is the branch itself, not a condition object.
 *
 * @param parent     Node to insert into.
 * @param child      Node to insert (must be currently parentless).
 * @param condition  true inserts under DCG_TRUE_CONDITION, false under DCG_FALSE_CONDITION.
 * @param index      Position to insert at; >= child_count appends.
 * @return DCG_OK or a DCG_ERR_* code.
 */
static inline int c_dcg_node_append_at_binary(dcg_node* parent, dcg_node* child, bool condition, size_t index) {
    return c_dcg_node_append_at(parent, child, condition ? DCG_TRUE_CONDITION : DCG_FALSE_CONDITION, index);
}

/**
 * @brief Unlink a node from its parent, keeping its own subtree intact.
 *
 * The node keeps its subtree but loses its edge: the condition falls back
 * to NO_CONDITION, exactly as a displaced node ends up in the capi.
 *
 * @param node  Node to unlink (NULL-safe: a parentless node is a no-op).
 * @return DCG_OK, or DCG_ERR_BUSY when the parent is FROZEN.
 */
static inline int c_dcg_node_detach(dcg_node* node) {
    if (!node || !node->parent) return DCG_OK;
    if (node->parent->flags & DCG_NODE_FLAG_FROZEN) return DCG_ERR_BUSY;

    dcg_node* parent = node->parent;
    c_dcg_node_unlink(node);
    c_dcg_node_invoke_callbacks(parent, DCG_NODE_EVENT_CHILD_REMOVED, node, (uint64_t) -1);
    return DCG_OK;
}

/**
 * @brief Put a node into the slot of another one, inheriting its edge.
 *
 * The replacement takes over the old node's position and condition; the old
 * node is detached but NOT freed (its subtree stays alive), so the caller
 * decides what happens to it.
 *
 * @param old_node  Node currently in the graph.
 * @param new_node  Node to put in its place (must be parentless).
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_BUSY or DCG_ERR_CYCLE.
 */
static inline int c_dcg_node_replace(dcg_node* old_node, dcg_node* new_node) {
    if (!old_node || !new_node) return DCG_ERR_INVALID_ARG;
    if (old_node == new_node) return DCG_OK;
    if (new_node->parent) return DCG_ERR_BUSY;
    if (c_dcg_node_is_ancestor(old_node, new_node)) return DCG_ERR_CYCLE;

    dcg_node* parent = old_node->parent;
    if (!parent) return DCG_ERR_INVALID_ARG;

    const dcg_node_edge_condition* condition = old_node->condition_to_parent;
    dcg_node*                      anchor    = old_node->next_sibling;

    int                            ret = c_dcg_node_detach(old_node);
    if (ret != DCG_OK) return ret;

    // The slot is reused as-is, so the else-ordering rule cannot be violated
    // here: link straight into the anchor instead of going through append_at.
    // link() adopts the condition onto the replacement, so an owned condition
    // travels with the edge instead of dying with the displaced node.
    return c_dcg_node_link(parent, new_node, condition, anchor);
}

/**
 * @brief Put a node into another's slot when a parent elsewhere already holds it.
 *
 * c_dcg_node_replace() refuses a replacement that has a parent, and in a tree
 * that is right: one node, one parent. A control-flow JOIN is the exception.
 * The node a breakpoint resumes into is reached from the breakpoint AND goes on
 * being the branch it was entered as, so it has to be placed here while the
 * parent that handed it over keeps its pointer to it - which is what the capi
 * does when it re-parents the node a breakpoint just connected to.
 *
 * What moves is the TREE link, not the list entry: the node's `parent` becomes
 * the node it displaces, while its former parent goes on naming it as a child.
 * That former parent is then a second entry point to the same block - a walk
 * reaches it twice and frees it once, which is what c_dcg_node_teardown_root()
 * is built to survive.
 *
 * It is representable only while the node is ALONE in the list it is leaving,
 * because the sibling links belong to the list being entered: a node with
 * neighbours still attached would leave them pointing into a chain it is no
 * longer in. A node that is not alone is therefore refused rather than
 * silently corrupting the walk that reads those links.
 *
 * @param old_node  Node currently in the graph.
 * @param new_node  Node to put in its place (alone in its own list, if it has one).
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_BUSY or DCG_ERR_CYCLE.
 */
static inline int c_dcg_node_replace_shared(dcg_node* old_node, dcg_node* new_node) {
    if (!old_node || !new_node) return DCG_ERR_INVALID_ARG;
    if (!old_node->parent) return DCG_ERR_INVALID_ARG;
    if (new_node->next_sibling || new_node->prev_sibling) return DCG_ERR_BUSY; /* not alone in its own list */

    /* Shed the tree link, keep the list entry: the former parent is what makes
     * this a join, and dropping its pointer would make it nothing. */
    new_node->parent = NULL;

    return c_dcg_node_replace(old_node, new_node);
}

/**
 * @brief Allocate an auto-generated placeholder.
 *
 * A placeholder is what an unfinished branch leaves behind. It is a plain node,
 * not an action: it carries none of the fields the action family's struct exists
 * for - no signal, no payload, no connect flag - and it is flagged autogen so
 * consolidation can tell it from a node the caller built.
 *
 * The opening half of the placeholder discipline: a branch reserves its slot
 * with this and fills it later, and whatever is still a placeholder when the
 * graph closes is retired by c_dcg_node_consolidate_placeholder below.
 *
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_node* c_dcg_node_new_placeholder(allocator_protocol* allocator) {
    dcg_node* node = c_dcg_node_new(DCG_NODE_PLACEHOLDER, DCG_DEF_REPR_PLACEHOLDER, allocator);
    if (!node) return NULL;

    node->autogen = true;
    return node;
}

/**
 * @brief Turn every placeholder of a subtree into an auto-generated no-action.
 *
 * The closing half of the placeholder discipline: the builder reserves a
 * branch with a placeholder and fills it later, so whatever is STILL a
 * placeholder when the graph closes becomes a NoAction leaf - a baked graph
 * never contains a placeholder.
 *
 * The conversion happens in place: the node keeps its slot, its edge, its
 * labels, its hooks and whatever subtree it grew, and only its type and its
 * display text change (reported as a MODIFIED mutation). The retitle is the
 * one step that allocates, so a node whose new text cannot be had keeps the
 * text it had and says so on stderr; the type has already flipped either way,
 * which is the structural half. A placeholder that was left holding children
 * keeps them - and validation will say so, which is the honest report of a
 * builder that never filled the slot properly.
 *
 * The walk is depth-first over the whole subtree and idempotent, so it can
 * be called once at the root or at every level like the capi does.
 *
 * @param node  Subtree to consolidate (NULL-safe).
 * @return Number of placeholders converted.
 */
static inline size_t c_dcg_node_consolidate_placeholder(dcg_node* node) {
    if (!node) return 0;

    size_t replaced = 0;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        replaced += c_dcg_node_consolidate_placeholder(child);

        if (child->ntype == DCG_NODE_PLACEHOLDER) {
            child->ntype   = DCG_NODE_NOACTION;
            child->autogen = true;
            if (c_dcg_node_set_repr(child, DCG_DEF_REPR_NOACTION) != DCG_OK) {
                (void) fprintf(stderr, "c_dcg_node_consolidate_placeholder: no text for the no-action at %p - keeping \"%s\"\n", (void*) child, child->repr ? child->repr : "");
            }
            c_dcg_node_invoke_callbacks(child, DCG_NODE_EVENT_MODIFIED, child, (uint64_t) -1);
            replaced++;
        }
    }
    return replaced;
}

/**
 * @brief The placeholder slot under a node: the one already reserved, or a new one.
 *
 * The opening half of the placeholder discipline - the closing half is
 * c_dcg_node_consolidate_placeholder(). A branch that is about to be built
 * reserves its slot first, and whatever fills the slot replaces the
 * placeholder; a slot nothing ever fills becomes a no-action when the graph
 * closes.
 *
 * The rules are the capi's, in its order:
 *
 *   - the NEWEST existing placeholder is reused, so a build that descends into
 *     a node fills the slot that was reserved last rather than reserving a
 *     second. Newest and not first, because a node entered by a `with` block
 *     reserves its two arms in the order FALSE then TRUE, and the capi - whose
 *     stack puts the newest at index 0 - fills the TRUE arm first. Taking the
 *     first placeholder here would fill the arms in the opposite order and
 *     build a different graph;
 *   - otherwise a placeholder is appended on the INFERRED edge, which is what
 *     makes this fail exactly when the node has no room left for a branch -
 *     a node with one non-binary child and no free edge, say. A root is no
 *     exception: the inference hands back the unconditioned edge it requires.
 *
 * The placeholder is allocated from the same allocator the node came from, so
 * the two are one allocation family; it is NOT nested under the node, because
 * a node does not own its children (see c_dcg_node_free).
 *
 * @param node  Node to reserve a slot under (NULL-safe).
 * @return The placeholder, or NULL on OOM / when no edge can be inferred.
 */
static inline dcg_node* c_dcg_node_get_placeholder(dcg_node* node) {
    if (!node) return NULL;

    dcg_node* newest = NULL;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        if (child->ntype == DCG_NODE_PLACEHOLDER) newest = child;
    }
    if (newest) return newest;

    dcg_node* placeholder = c_dcg_node_new_placeholder(c_ap_protocol_from_ptr(node));
    if (!placeholder) return NULL;

    if (c_dcg_node_append_auto(node, placeholder) != DCG_OK) {
        c_dcg_node_free(placeholder);
        return NULL;
    }
    return placeholder;
}

// ========== Queries ==========

/**
 * @brief Number of direct children.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The child count.
 */
static inline size_t c_dcg_node_child_count(const dcg_node* node) {
    if (!node) return 0;
    size_t    count = 0;
    dcg_node* child = node->children;
    while (child) {
        count++;
        child = child->next_sibling;
    }
    return count;
}

/**
 * @brief First child of a node.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The first child, or NULL for a leaf.
 */
static inline dcg_node* c_dcg_node_first_child(const dcg_node* node) {
    if (!node) return NULL;
    return node->children;
}

/**
 * @brief Last child of a node.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The last child, or NULL for a leaf.
 */
static inline dcg_node* c_dcg_node_last_child(const dcg_node* node) {
    if (!node) return NULL;
    dcg_node* child = node->children;
    if (!child) return NULL;
    while (child->next_sibling) child = child->next_sibling;
    return child;
}

/**
 * @brief Child at a position.
 *
 * @param node   Node to inspect (NULL-safe).
 * @param index  Zero-based position.
 * @return The child, or NULL when out of range.
 */
static inline dcg_node* c_dcg_node_child_at(const dcg_node* node, size_t index) {
    if (!node) return NULL;
    dcg_node* child = node->children;
    while (child && index > 0) {
        child = child->next_sibling;
        index--;
    }
    return child;
}

/**
 * @brief Position of a node among its siblings.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The zero-based index, or -1 for the root (a node without a parent).
 */
static inline ssize_t c_dcg_node_child_index(const dcg_node* node) {
    if (!node || !node->parent) return -1;

    ssize_t   index = 0;
    dcg_node* child = node->parent->children;
    while (child) {
        if (child == node) return index;
        index++;
        child = child->next_sibling;
    }
    return -1;
}

/**
 * @brief First child registered under a given edge condition.
 *
 * Conditions compare by identity for the built-ins and by payload for the
 * rest (see c_dcg_condition_equals), which is what backs both the capi's
 * condition-keyed children dict and the one-edge-per-condition rule.
 *
 * @param node       Node to inspect (NULL-safe).
 * @param condition  Condition to look for (NULL = the unconditional edge).
 * @return The child, or NULL when there is none.
 */
static inline dcg_node* c_dcg_node_child_by_condition(const dcg_node* node, const dcg_node_edge_condition* condition) {
    if (!node) return NULL;
    dcg_node* child = node->children;
    while (child) {
        if (c_dcg_condition_equals(child->condition_to_parent, condition)) return child;
        child = child->next_sibling;
    }
    return NULL;
}

/**
 * @brief Next sibling of a node.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The next sibling, or NULL.
 */
static inline dcg_node* c_dcg_node_next_sibling(const dcg_node* node) {
    if (!node) return NULL;
    return node->next_sibling;
}

/**
 * @brief Previous sibling of a node.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The previous sibling, or NULL.
 */
static inline dcg_node* c_dcg_node_prev_sibling(const dcg_node* node) {
    if (!node) return NULL;
    return node->prev_sibling;
}

/**
 * @brief Walk up to the topmost ancestor - the root of the graph.
 *
 * @param node  Node to start from (NULL-safe).
 * @return The root, or NULL.
 */
static inline dcg_node* c_dcg_node_root(dcg_node* node) {
    if (!node) return NULL;
    while (node->parent) node = node->parent;
    return node;
}

/**
 * @brief Number of edges between a node and its root.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The depth (0 for a root).
 */
static inline size_t c_dcg_node_depth(const dcg_node* node) {
    size_t depth = 0;
    while (node && node->parent) {
        depth++;
        node = node->parent;
    }
    return depth;
}

/**
 * @brief Longest downward path from a node to one of its leaves.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The height (0 for a leaf).
 */
static inline size_t c_dcg_node_height(const dcg_node* node) {
    if (!node || !node->children) return 0;

    size_t best = 0;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        size_t h = c_dcg_node_height(child);
        if (h > best) best = h;
    }
    return best + 1;
}

/**
 * @brief Number of nodes in the subtree rooted at a node (the node included).
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The subtree size.
 */
static inline size_t c_dcg_node_subtree_size(const dcg_node* node) {
    if (!node) return 0;
    size_t size = 1;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        size += c_dcg_node_subtree_size(child);
    }
    return size;
}

/**
 * @brief Number of leaves in the subtree rooted at a node.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The leaf count (a childless node counts as one leaf).
 */
static inline size_t c_dcg_node_leaf_count(const dcg_node* node) {
    if (!node) return 0;
    if (!node->children) return 1;

    size_t count = 0;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        count += c_dcg_node_leaf_count(child);
    }
    return count;
}

/**
 * @brief Predicate: does the node have no children?
 *
 * @param node  Node to inspect (NULL-safe).
 * @return true for a leaf.
 */
static inline bool c_dcg_node_is_leaf(const dcg_node* node) {
    return node && node->children == NULL;
}

/**
 * @brief Predicate: is the node parentless?
 *
 * @param node  Node to inspect (NULL-safe).
 * @return true for the root.
 */
static inline bool c_dcg_node_is_root(const dcg_node* node) {
    return node && node->parent == NULL;
}

/**
 * @brief Predicate: is one node an ancestor of another?
 *
 * @param node        Candidate ancestor (NULL-safe).
 * @param descendant  Candidate descendant (NULL-safe).
 * @return true when descendant sits anywhere below node.
 */
static inline bool c_dcg_node_is_ancestor(const dcg_node* node, const dcg_node* descendant) {
    if (!node || !descendant) return false;
    for (const dcg_node* walk = descendant->parent; walk; walk = walk->parent) {
        if (walk == node) return true;
    }
    return false;
}

/**
 * @brief Depth-first search for the first node carrying a uid.
 *
 * @param root  Subtree to search (NULL-safe).
 * @param uid   Identity to look for (16 bytes).
 * @return The node, or NULL when there is none.
 */
static inline dcg_node* c_dcg_node_find_by_uid(const dcg_node* root, const uuid_t uid) {
    if (!root || !uid) return NULL;
    if (memcmp(root->uid, uid, sizeof(uuid_t)) == 0) return (dcg_node*) root;

    for (dcg_node* child = root->children; child; child = child->next_sibling) {
        dcg_node* found = c_dcg_node_find_by_uid(child, uid);
        if (found) return found;
    }
    return NULL;
}

/**
 * @brief Recursive worker of c_dcg_node_collect_leaves.
 *
 * @param node     Subtree to walk.
 * @param out      Destination array (may be NULL to only count).
 * @param cap      Capacity of out.
 * @param written  Running number of entries already stored (NULL-safe).
 * @return Number of leaves in this subtree.
 */
static inline size_t c_dcg_node_collect_leaves_walk(const dcg_node* node, dcg_node** out, size_t cap, size_t* written) {
    if (!node) return 0;

    if (!node->children) {
        if (out && written && *written < cap) out[*written] = (dcg_node*) node;
        if (written) (*written)++;
        return 1;
    }

    size_t total = 0;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        total += c_dcg_node_collect_leaves_walk(child, out, cap, written);
    }
    return total;
}

/**
 * @brief Collect the leaves of a subtree, depth-first, left to right.
 *
 * @param root  Subtree to walk (NULL-safe).
 * @param out   Destination array (may be NULL to only count).
 * @param cap   Capacity of out.
 * @return The total number of leaves; when it exceeds cap, the array holds
 *         the first cap of them.
 */
static inline size_t c_dcg_node_collect_leaves(const dcg_node* root, dcg_node** out, size_t cap) {
    size_t written = 0;
    return c_dcg_node_collect_leaves_walk(root, out, cap, &written);
}

/**
 * @brief Recursive worker of c_dcg_node_collect_descendants.
 *
 * @param node     Subtree to walk.
 * @param out      Destination array (may be NULL to only count).
 * @param cap      Capacity of out.
 * @param written  Running number of entries already stored (NULL-safe).
 * @return Number of descendants in this subtree.
 */
static inline size_t c_dcg_node_collect_descendants_walk(const dcg_node* node, dcg_node** out, size_t cap, size_t* written) {
    if (!node) return 0;

    size_t total = 0;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        if (out && written && *written < cap) out[*written] = (dcg_node*) child;
        if (written) (*written)++;
        total++;
        total += c_dcg_node_collect_descendants_walk(child, out, cap, written);
    }
    return total;
}

/**
 * @brief Collect every node of a subtree below the root, pre-order.
 *
 * @param root  Subtree to walk (NULL-safe).
 * @param out   Destination array (may be NULL to only count).
 * @param cap   Capacity of out.
 * @return The total number of descendants; when it exceeds cap, the array
 *         holds the first cap of them.
 */
static inline size_t c_dcg_node_collect_descendants(const dcg_node* root, dcg_node** out, size_t cap) {
    size_t written = 0;
    return c_dcg_node_collect_descendants_walk(root, out, cap, &written);
}

/**
 * @brief Collect the chain of nodes from the root down to a node.
 *
 * The node itself is the last entry. Useful for recording an eval path.
 *
 * @param node  Node to trace (NULL-safe).
 * @param out   Destination array (may be NULL to only count).
 * @param cap   Capacity of out.
 * @return The path length (root..node inclusive); entries beyond cap are
 *         dropped from the front when the path does not fit.
 */
static inline size_t c_dcg_node_path_to(const dcg_node* node, const dcg_node** out, size_t cap) {
    size_t depth = c_dcg_node_depth(node);
    for (size_t i = 0; i <= depth && node; i++) {
        size_t index = depth - i;
        if (out && index < cap) out[index] = node;
        node = node->parent;
    }
    return depth + 1;
}

// ========== Labels ==========

/**
 * @brief Attach a label to a node, owning a copy of its text.
 *
 * The text is copied into a block nested under the node, and the list entry
 * itself is a nested block too, so removing the label - or freeing the node -
 * releases both without a separate step. Duplicates are refused.
 *
 * @param node   Node to label.
 * @param label  Label text to copy.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_INVALID_BUF or DCG_ERR_TYPE.
 */
static inline int c_dcg_node_add_label(dcg_node* node, const char* label) {
    if (!node || !label) return DCG_ERR_INVALID_ARG;
    if (c_dcg_node_has_label(node, label)) return DCG_ERR_TYPE;

    dcg_node_label* entry = (dcg_node_label*) c_ap_alloc_child(sizeof(dcg_node_label), NULL, node);
    if (!entry) return DCG_ERR_OOM;

    size_t len  = strlen(label);
    char*  copy = (char*) c_ap_alloc_child(len + 1, NULL, entry);
    if (!copy) {
        c_ap_free_owned(entry);
        return DCG_ERR_OOM;
    }
    memcpy(copy, label, len + 1);

    entry->label = copy;
    entry->next  = node->labels;
    node->labels = entry;

    c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_MODIFIED, node, (uint64_t) -1);
    return DCG_OK;
}

/**
 * @brief Predicate: does the node carry a label?
 *
 * @param node   Node to inspect (NULL-safe).
 * @param label  Label text to look for.
 * @return true when the label is attached.
 */
static inline bool c_dcg_node_has_label(const dcg_node* node, const char* label) {
    if (!node || !label) return false;
    for (const dcg_node_label* entry = node->labels; entry; entry = entry->next) {
        if (entry->label && strcmp(entry->label, label) == 0) return true;
    }
    return false;
}

/**
 * @brief Detach a label from a node, releasing its copy of the text.
 *
 * @param node   Node to modify.
 * @param label  Label text to remove.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_NOT_FOUND.
 */
static inline int c_dcg_node_remove_label(dcg_node* node, const char* label) {
    if (!node || !label) return DCG_ERR_INVALID_ARG;

    dcg_node_label* prev = NULL;
    dcg_node_label* curr = node->labels;
    while (curr) {
        if (curr->label && strcmp(curr->label, label) == 0) {
            if (prev) prev->next = curr->next;
            else node->labels = curr->next;
            if (curr->label) c_ap_free_owned((void*) curr->label);
            c_ap_free_owned(curr);
            c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_MODIFIED, node, (uint64_t) -1);
            return DCG_OK;
        }
        prev = curr;
        curr = curr->next;
    }
    return DCG_ERR_NOT_FOUND;
}

/**
 * @brief Number of labels attached to a node.
 *
 * @param node  Node to inspect (NULL-safe).
 * @return The label count.
 */
static inline size_t c_dcg_node_label_count(const dcg_node* node) {
    if (!node) return 0;
    size_t count = 0;
    for (const dcg_node_label* entry = node->labels; entry; entry = entry->next) count++;
    return count;
}

// ========== Validation ==========

/**
 * @brief Record a validation problem and mark the graph invalid.
 *
 * @param report  Report being filled (NULL-safe: counting is then skipped).
 * @param node    Node the problem was found on.
 * @param err     Error code.
 * @param valid   Validity accumulator (NULL-safe).
 */
static inline void c_dcg_validate_fail(dcg_validate_report* report, dcg_node* node, dcg_ret_code err, bool* valid) {
    if (report) {
        report->errors++;
        if (report->code == DCG_OK) {
            report->code = err;
            report->node = node;
        }
    }
    if (valid) *valid = false;
}

/**
 * @brief Recursive worker of c_dcg_node_validate.
 *
 * @param node    Node to check.
 * @param report  Report being filled (NULL-safe).
 * @param depth   Depth of node.
 * @return true when the subtree is valid.
 */
static inline bool c_dcg_node_validate_walk(const dcg_node* node, dcg_validate_report* report, size_t depth) {
    if (!node) return true;
    if (report) {
        report->nodes++;
        if (depth > report->depth) report->depth = depth;
    }

    bool   valid = true;
    size_t count = c_dcg_node_child_count(node);

    if (node->ntype == DCG_NODE_ROOT) {
        const dcg_node* entry = node->children;
        if (node->parent) c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_TYPE, &valid);
        if (count > 1) c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_TYPE, &valid);
        if (entry && !c_dcg_condition_is_none(entry->condition_to_parent)) {
            c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_EDGE, &valid);
        }
    }
    else if (node->parent == NULL) {
        c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_TYPE, &valid);
    }

    if (c_dcg_node_type_is_action(node->ntype) && count > 0) {
        c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_TYPE, &valid);
    }

    size_t arity = c_dcg_node_type_arity(node->ntype);
    if (arity > 0 && count != arity) c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_TYPE, &valid);

    size_t    index             = 0;
    dcg_node* else_child        = NULL;
    bool      has_binary        = false;
    bool      has_value         = false;
    bool      has_unconditioned = false;

    for (dcg_node* child = node->children; child; child = child->next_sibling, index++) {
        const dcg_node_edge_condition* cond = child->condition_to_parent;

        if (c_dcg_condition_is_auto(cond)) {
            c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_UNRESOLVED, &valid);
        }

        if (c_dcg_condition_is_else(cond)) {
            if (else_child) c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_DUPLICATE, &valid);
            else_child = child;
            if (index + 1 != count) c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_EDGE, &valid);
        }

        // Branch shapes: a binary branch, a value branch, an else fallback (a
        // legitimate branch), or the unconditional/auto pair.
        if (c_dcg_condition_is_binary(cond)) has_binary = true;
        else if (c_dcg_condition_is_else(cond)) { /* the fallback is a branch of its own */
        }
        else if (c_dcg_condition_is_sentinel(cond)) has_unconditioned = true;
        else has_value = true;

        if (c_dcg_node_is_ancestor(child, node)) {
            c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_CYCLE, &valid);
            continue;  // Report it and stop - descending would never come back.
        }

        if (!c_dcg_node_validate_walk(child, report, depth + 1)) valid = false;
    }

    // Branch-shape rules apply to decision nodes only: a root is unconditioned
    // by construction, and everything else branches.
    if (count > 1 && node->ntype != DCG_NODE_ROOT) {
        if (has_unconditioned) c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_EDGE, &valid);
        if (has_binary && has_value) c_dcg_validate_fail(report, (dcg_node*) node, DCG_ERR_EDGE, &valid);
    }

    return valid;
}

/**
 * @brief Validate a graph, walking it depth-first from the given node.
 *
 * Enforces the structural invariants a baked graph must satisfy: exactly one
 * parentless ROOT, at most one child under a root, fixed operator arity,
 * action nodes as leaves, at most one trailing ELSE per node, resolved
 * conditions, consistent branch shapes, and acyclicity.
 *
 * Problems are reported as dcg_ret_code values, so a caller handles a bad
 * graph with the same vocabulary it handles every other failure:
 *   - DCG_ERR_TYPE        a detached fragment, a root with too many children,
 *                         a wrong operand count, an action holding children
 *   - DCG_ERR_EDGE        a root edge that is not unconditional, an else that
 *                         is not last, conflicting or unconditioned branches
 *   - DCG_ERR_DUPLICATE   more than one else branch
 *   - DCG_ERR_UNRESOLVED  an AUTO condition that survived into the graph
 *   - DCG_ERR_CYCLE       a cycle (reported, then the walk stops there)
 *
 * A cycle is reported and the walk stops there instead of descending into
 * it. The other tree walks (teardown, size and leaf queries, rendering)
 * assume an acyclic graph - this is the check that establishes it.
 *
 * @param root    Node to validate from (typically the graph root).
 * @param report  Receives the outcome; may be NULL when only the verdict matters.
 * @return true when the graph is valid.
 */
static inline bool c_dcg_node_validate(const dcg_node* root, dcg_validate_report* report) {
    if (report) memset(report, 0, sizeof(*report));
    if (!root) return false;
    return c_dcg_node_validate_walk(root, report, 0);
}

/**
 * @brief Validate a graph and print every problem found, one per line.
 *
 * The report is produced by a plain validation run; this only renders it.
 *
 * @param root    Node to validate from.
 * @param stream  Destination stream.
 * @return Number of errors found, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_node_validate_print(const dcg_node* root, FILE* stream) {
    if (!stream || !root) return DCG_ERR_INVALID_ARG;

    dcg_validate_report report;
    bool                valid = c_dcg_node_validate(root, &report);

    (void) fprintf(
        stream,
        "validate: %s nodes=%zu depth=%zu errors=%zu first=%s\n",
        valid ? "OK" : "INVALID",
        report.nodes,
        report.depth,
        report.errors,
        c_dcg_ret_code_name(report.code)
    );
    if (!valid && report.node) {
        (void) fprintf(stream, "  first error at %s \"%s\"\n", c_dcg_node_type_name(report.node->ntype), report.node->repr ? report.node->repr : "");
    }
    return (int) report.errors;
}

// ========== Rendering ==========

/**
 * @brief Fill in the standard renderer options.
 *
 * @param opts  Options to populate (NULL-safe).
 */
static inline void c_dcg_render_opts_default(dcg_render_opts* opts) {
    if (!opts) return;
    memset(opts, 0, sizeof(*opts));
    opts->style          = DCG_RENDER_UNICODE;
    opts->max_depth      = DCG_NODE_RENDER_MAX_DEPTH;
    opts->show_condition = true;
    opts->show_out       = true;
    opts->show_children  = true;
}

/**
 * @brief Render a node's flags as a compact string.
 *
 * @param flags  Flag bits.
 * @param out    Destination buffer.
 * @param cap    Capacity of out.
 * @return Number of characters written, or DCG_ERR_*.
 */
static inline int c_dcg_node_format_flags(uint32_t flags, char* out, size_t cap) {
    if (!out || cap == 0) return DCG_ERR_INVALID_ARG;

    static const struct {
        uint32_t    bit;
        const char* name;
    } entries[] = {
        {DCG_NODE_FLAG_FROZEN, "frozen"},
        {DCG_NODE_FLAG_VISITED, "visited"},
        {DCG_NODE_FLAG_PRUNED, "pruned"},
    };

    dcg_strbuf buf;
    c_dcg_sb_init(&buf, out, cap);

    size_t matched = 0;
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (!(flags & entries[i].bit)) continue;
        c_dcg_sb_printf(&buf, "%s%s", matched ? "|" : "", entries[i].name);
        matched++;
    }
    if (matched == 0) c_dcg_sb_puts(&buf, "-");

    return buf.used >= buf.cap ? DCG_ERR_FULL : (int) buf.used;
}

/**
 * @brief Render a node's uid as a 32-character hex string (no dashes).
 *
 * Byte order is the array's own: the first byte of the field leads, which is
 * what makes the string a straight hex dump of the 16 bytes `uuid.UUID.bytes`
 * hands back.
 *
 * @param uid  Identity to render (16 bytes).
 * @param out  Destination buffer (at least 33 bytes, terminator included).
 * @param cap  Capacity of out.
 * @return Number of characters written, or DCG_ERR_*.
 */
static inline int c_dcg_node_format_uid(const uuid_t uid, char* out, size_t cap) {
    if (!uid || !out) return DCG_ERR_INVALID_ARG;
    if (cap < sizeof(uuid_t) * 2 + 1) return DCG_ERR_FULL;

    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(uuid_t); i++) {
        out[i * 2]     = hex[(uid[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[uid[i] & 0x0F];
    }
    out[sizeof(uuid_t) * 2] = '\0';

    return (int) (sizeof(uuid_t) * 2);
}

/**
 * @brief Render one node's own line (without the tree prefix).
 *
 * @param node    Node to render.
 * @param out     Destination buffer.
 * @param cap     Capacity of out.
 * @param opts    Renderer options (NULL = defaults).
 * @return Number of characters written, or DCG_ERR_*.
 */
static inline int c_dcg_node_format_line(const dcg_node* node, char* out, size_t cap, const dcg_render_opts* opts) {
    if (!node || !out || cap == 0) return DCG_ERR_INVALID_ARG;

    dcg_render_opts dflt;
    if (!opts) {
        c_dcg_render_opts_default(&dflt);
        opts = &dflt;
    }

    dcg_strbuf buf;
    c_dcg_sb_init(&buf, out, cap);

    char scratch[DCG_NODE_STRING_MAXLEN];

    if (opts->show_condition) {
        (void) c_dcg_condition_format(node->condition_to_parent, "", scratch, sizeof(scratch));
        c_dcg_sb_printf(&buf, "[%s] ", scratch);
    }

    /* The operator itself lives in the expression variant, which this header
     * does not see; an expression's repr carries its symbol (see c_expr.h). */
    c_dcg_sb_puts(&buf, c_dcg_node_type_name(node->ntype));

    if (node->repr) c_dcg_sb_printf(&buf, " \"%s\"", node->repr);

    if (opts->show_out) {
        (void) c_dcg_var_format(&node->out, scratch, sizeof(scratch));
        c_dcg_sb_printf(&buf, " out=%s", scratch);
    }

    if (opts->show_children) c_dcg_sb_printf(&buf, " children=%zu", c_dcg_node_child_count(node));

    if (opts->show_hooks) {
        c_dcg_sb_puts(&buf, " hooks=[");
        if (node->eval_ctx.pre_eval_fn) c_dcg_sb_puts(&buf, "pre");
        if (node->eval_ctx.eval_fn) c_dcg_sb_printf(&buf, "%seval", node->eval_ctx.pre_eval_fn ? "," : "");
        if (node->eval_ctx.post_eval_fn) c_dcg_sb_printf(&buf, "%spost", (node->eval_ctx.pre_eval_fn || node->eval_ctx.eval_fn) ? "," : "");
        c_dcg_sb_puts(&buf, "]");
    }

    if (opts->show_labels && node->labels) {
        c_dcg_sb_puts(&buf, " labels=[");
        size_t index = 0;
        for (const dcg_node_label* entry = node->labels; entry; entry = entry->next, index++) {
            c_dcg_sb_printf(&buf, "%s%s", index ? "," : "", entry->label ? entry->label : "?");
        }
        c_dcg_sb_puts(&buf, "]");
    }

    if (opts->show_flags && node->flags) {
        (void) c_dcg_node_format_flags(node->flags, scratch, sizeof(scratch));
        c_dcg_sb_printf(&buf, " flags=%s", scratch);
    }

    if (opts->show_uid) {
        (void) c_dcg_node_format_uid(node->uid, scratch, sizeof(scratch));
        c_dcg_sb_printf(&buf, " uid=%s", scratch);
    }

    if (opts->show_address) c_dcg_sb_printf(&buf, " @%p", (const void*) node);

    return buf.used >= buf.cap ? DCG_ERR_FULL : (int) buf.used;
}

/**
 * @brief Recursive worker of c_dcg_node_render.
 *
 * @param node     Node to render.
 * @param stream   Destination stream.
 * @param opts     Renderer options.
 * @param prefix   Prefix drawn to the left of this node's line.
 * @param is_last  true when the node is the last child of its parent.
 * @param depth    Depth of the node.
 * @return Number of characters written, or DCG_ERR_*.
 */
static inline int c_dcg_node_render_walk(const dcg_node* node, FILE* stream, const dcg_render_opts* opts, const char* prefix, bool is_last, size_t depth) {
    if (!node) return 0;

    char line[DCG_NODE_STRING_MAXLEN * 2];
    int  n = c_dcg_node_format_line(node, line, sizeof(line), opts);
    if (n < 0) return n;

    bool        unicode = (opts->style == DCG_RENDER_UNICODE);
    const char* guide   = "";
    if (depth > 0) guide = is_last ? (unicode ? "└── " : "`-- ") : (unicode ? "├── " : "|-- ");

    int written = fprintf(stream, "%s%s%s\n", prefix, guide, line);
    if (written < 0) return DCG_ERR_FORMAT;

    char       child_prefix[DCG_NODE_STRING_MAXLEN];
    dcg_strbuf buf;
    c_dcg_sb_init(&buf, child_prefix, sizeof(child_prefix));
    c_dcg_sb_puts(&buf, prefix);
    if (depth > 0) c_dcg_sb_puts(&buf, is_last ? "    " : (unicode ? "│   " : "|   "));

    if (opts->max_depth && depth + 1 > opts->max_depth) {
        if (node->children) {
            n = fprintf(stream, "%s... (%zu children hidden)\n", child_prefix, c_dcg_node_child_count(node));
            if (n < 0) return DCG_ERR_FORMAT;
            written += n;
        }
        return written;
    }

    size_t remaining = c_dcg_node_child_count(node);
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        remaining--;
        n = c_dcg_node_render_walk(child, stream, opts, child_prefix, remaining == 0, depth + 1);
        if (n < 0) return n;
        written += n;
    }
    return written;
}

/**
 * @brief Render a node and its subtree to a stream, one node per line.
 *
 * The root line carries no branch glyph; every descendant is prefixed with
 * the usual box-drawing (or ASCII) guide.
 *
 * @param node    Node to render (typically the graph root).
 * @param stream  Destination stream.
 * @param opts    Renderer options (NULL = defaults).
 * @return Number of characters written, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_node_render(const dcg_node* node, FILE* stream, const dcg_render_opts* opts) {
    if (!node || !stream) return DCG_ERR_INVALID_ARG;

    dcg_render_opts dflt;
    if (!opts) {
        c_dcg_render_opts_default(&dflt);
        opts = &dflt;
    }
    return c_dcg_node_render_walk(node, stream, opts, "", true, 0);
}

/**
 * @brief Render a node and its subtree to stdout with the default options.
 *
 * @param node  Node to render (NULL-safe).
 * @return Number of characters written, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_node_print(const dcg_node* node) {
    if (!node) return DCG_ERR_INVALID_ARG;
    return c_dcg_node_render(node, stdout, NULL);
}

/**
 * @brief Render a node and its subtree into a caller-provided buffer.
 *
 * Rendering goes through a fixed-size stream, so the buffer must be able to
 * hold the whole subtree: a buffer that runs out is reported as
 * DCG_ERR_FULL rather than a half-written tree, and the buffer is still
 * NUL-terminated.
 *
 * @param node  Node to render.
 * @param out   Destination buffer.
 * @param cap   Capacity of out.
 * @param opts  Renderer options (NULL = defaults).
 * @return Number of characters written (excluding NUL), or DCG_ERR_INVALID_ARG
 *         / DCG_ERR_FULL / DCG_ERR_TYPE (Windows, no fmemopen).
 */
static inline int c_dcg_node_render_to_string(const dcg_node* node, char* out, size_t cap, const dcg_render_opts* opts) {
    if (!node || !out || cap == 0) return DCG_ERR_INVALID_ARG;

#if defined(_WIN32)
    // fmemopen has no Windows counterpart; use the stream renderer there.
    (void) opts;
    return DCG_ERR_TYPE;
#else
    FILE* stream = fmemopen(out, cap, "w");
    if (!stream) return DCG_ERR_FORMAT;

    int  written = c_dcg_node_render(node, stream, opts);
    long end     = ftell(stream);
    (void) fflush(stream);
    (void) fclose(stream);

    // Inside a fixed-size stream every failure is a buffer overflow: the
    // line formatter reports FULL, while a full buffer makes fprintf fail
    // outright. Both mean "the tree did not fit".
    out[cap - 1] = '\0';
    if (written < 0) return DCG_ERR_FULL;
    if ((end < 0) || ((size_t) end >= cap)) return DCG_ERR_FULL;

    return written;
#endif
}

#endif  // C_DCG_BAKE_NODE_H