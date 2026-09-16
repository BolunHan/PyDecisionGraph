#ifndef C_DCG_BAKE_LOGIC_GROUP_H
#define C_DCG_BAKE_LOGIC_GROUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>
#include <cbase/bytemap/c_bytemap.h>

#include <decision_graph/decision_tree/bake/c_action.h>
#include <decision_graph/decision_tree/bake/c_hierarchy.h>
#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The logic groups: the scopes a graph is built inside, and the manager that
 * keeps track of which one is open.
 *
 * A group is a NAME, A KIND and a PARENT - nothing else. What
 * makes it more than a record is that a build runs inside one: a node built
 * while groups are open carries their names as labels (which is how the capi's
 * list_labels() finds the nodes of a group), and breaking out of a group is
 * the only way a branch stops where it stands and resumes outside.
 *
 * The manager (bake's LGM) holds the three stacks a build runs on:
 *
 *   - the open groups, innermost last;
 *   - the nodes being built, innermost last - the active node is the one a
 *     branch belongs to right now;
 *   - the breakpoints raised and not yet connected back into the graph.
 *
 * It also holds the registry groups are looked up by name in, the shelved
 * states a sub-graph build puts the stacks away in (see c_dcg_lgm_shelve), and
 * the two mode flags a build switches on.
 *
 * What is NOT here is the runtime half of a break. In the capi, breaking while
 * building (the inspection mode) is what this header ports - the breakpoint is
 * raised into the graph and connected to whatever is entered next. Breaking
 * while EVALUATING is a control-flow signal, and control flow is the
 * evaluator's, so it lands with the evaluator and not before.
 *
 * Ownership, like everywhere else in bake, is explicit: the manager holds
 * BORROWED pointers to groups and nodes. A registry entry does not own its
 * group - a group outlives any number of lookups, and several variable nodes
 * may read the same one - so whoever built the groups releases them.
 */

// ========== Constants ==========

/** Groups a fresh manager's registry is created with. */
#ifndef DCG_LGM_DEFAULT_CAPACITY
#define DCG_LGM_DEFAULT_CAPACITY 16U
#endif

// ========== Structs ==========

/*
 * Forward-declared because the manager holds blocks of them: the struct itself
 * is private to this header, and only its pointer ever crosses out of it.
 */
typedef struct dcg_lgm_state dcg_lgm_state;

/**
 * @brief Kind of a logic group.
 *
 * One flat set rather than a family tree like the node kinds: every group is
 * the same struct - a name, a parent and a kind - and a variant is
 * the group that adds a store of its own beside them. The value is what a
 * caller dispatches on and what a baked artifact records.
 */
typedef enum dcg_logic_group_type {
    DCG_LG_BASE    = 0x0000,  // The base group: metadata and nothing else.
    DCG_LG_MAPPING = 0x0001   // A group whose entries live in a store of their own.
} dcg_logic_group_type;

/**
 * @brief A logic group: a named scope a graph is built inside.
 *
 * It holds METADATA and nothing else - what the group IS, and where it sits,
 * which is all a build needs to reach it and read it back. A group that has
 * values to carry is a dcg_mapping_lgroup: the store lives there, beside this
 * header, and a scope with no store of its own has none to leak.
 *
 * (The capi gives the base class a contexts dict as well, and keeps runtime
 * Python objects in it. Bake has no such need - a value a graph reads is a slot
 * a store owns, or a caller's own var - so the field is not ported rather than
 * ported empty.)
 *
 * `name` is owned: it is copied into a block nested under the group. It is the
 * group's identity - what a node's labels carry, and what the registry looks a
 * group up by - so it is the caller's to give and the group's to keep.
 *
 * `parent` is NOT owned. It is set when the group is entered with another
 * group already open (see c_dcg_lgm_enter_group), and it names a group that
 * outlives this one.
 *
 * A dcg_mapping_lgroup embeds this struct as its FIRST member, so a
 * dcg_mapping_lgroup* is a valid dcg_logic_group*.
 */
typedef struct dcg_logic_group {
    // === Meta ===
    dcg_logic_group_type lgtype;  // Group kind.
    const char*          name;    // Display and lookup name. // OWNED - a nested copy.
    dcg_logic_group*     parent;  // The group this one is nested in, or NULL. // NOT owned.
} dcg_logic_group;

/**
 * @brief The stacks a sub-graph build puts away, and the modes it ran under.
 *
 * One entry of the manager's shelf. The three stacks are WHOLE BLOCKS, not
 * copies: shelving hands the live blocks over and gives the manager empty ones,
 * so putting the state back is a swap of pointers rather than a copy of every
 * frame.
 */
struct dcg_lgm_state {
    dcg_logic_group**     groups;  // OWNED - the group stack that was live.
    size_t                n_groups;
    size_t                groups_capacity;
    dcg_node**            nodes;  // OWNED - the node stack that was live.
    size_t                n_nodes;
    size_t                nodes_capacity;
    dcg_breakpoint_node** breakpoints;  // OWNED - the breakpoint list that was live.
    size_t                n_breakpoints;
    size_t                breakpoints_capacity;
    bool                  inspection_mode;
    bool                  vigilant_mode;
};

/**
 * @brief The group manager: the build state a graph is assembled under.
 *
 * A caller-owned struct (a local, a field of a builder, or a block of its own)
 * whose blocks are allocator blocks; c_dcg_lgm_init() creates them and
 * c_dcg_lgm_free() releases them. Everything it points at is borrowed: the
 * groups in the registry and on the stacks, and the nodes on the node stack.
 *
 * It keeps the allocator it was initialized with, because it is NOT itself an
 * allocator block: a manager on the caller's stack has no allocator header to
 * read one back out of, which is the one thing a node can do and a manager
 * cannot.
 *
 * The three stacks are arrays with the innermost frame LAST, so the active
 * group is the last entry and a push is an append. (The capi keeps the
 * innermost first and inserts at index 0; the two are equivalent, and an array
 * that grows at its end is the one that needs no shifting.)
 */
typedef struct dcg_logic_group_manager {
    // === Allocator ===
    allocator_protocol*   allocator;  // How this manager's blocks are made and grown.
    // === Registry ===
    bytemap               registry;  // Group name -> dcg_logic_group* (stored as a uintptr_t). BORROWED.
    // === Open groups ===
    dcg_logic_group**     groups;  // Innermost last. OWNED block.
    size_t                n_groups;
    size_t                groups_capacity;
    // === Nodes being built ===
    dcg_node**            nodes;  // Innermost last: the active node is the last entry. OWNED block.
    size_t                n_nodes;
    size_t                nodes_capacity;
    // === Raised breakpoints ===
    dcg_breakpoint_node** breakpoints;  // Oldest first. OWNED block. BORROWED entries.
    size_t                n_breakpoints;
    size_t                breakpoints_capacity;
    // === Shelved states ===
    dcg_lgm_state*        shelved;  // Newest last. OWNED block.
    size_t                n_shelved;
    size_t                shelved_capacity;
    // === Modes ===
    bool                  inspection_mode;  // Building a layout rather than evaluating it.
    bool                  vigilant_mode;    // Refuse what a lenient build would tolerate.
} dcg_logic_group_manager;

// ========== Forward Declarations ==========

// Utilities
static inline const char*      c_dcg_logic_group_type_name(dcg_logic_group_type lgtype);

// Group lifecycle
static inline int              c_dcg_logic_group_init(dcg_logic_group* group, dcg_logic_group_type lgtype, const char* name);
static inline dcg_logic_group* c_dcg_logic_group_new(dcg_logic_group_type lgtype, const char* name, allocator_protocol* allocator);
static inline void             c_dcg_logic_group_free(dcg_logic_group* group);

// Manager lifecycle
static inline int              c_dcg_lgm_init(dcg_logic_group_manager* mgr, allocator_protocol* allocator);
static inline void             c_dcg_lgm_free(dcg_logic_group_manager* mgr);
static inline void             c_dcg_lgm_clear(dcg_logic_group_manager* mgr);

// Registry
static inline int              c_dcg_lgm_register(dcg_logic_group_manager* mgr, dcg_logic_group* group);
static inline dcg_logic_group* c_dcg_lgm_find(const dcg_logic_group_manager* mgr, const char* name, size_t name_len);

// Building inside the manager
static inline int              c_dcg_lgm_enter_group(dcg_logic_group_manager* mgr, dcg_logic_group* group);
static inline int              c_dcg_lgm_exit_group(dcg_logic_group_manager* mgr, dcg_logic_group* group);
static inline int              c_dcg_lgm_enter_node(dcg_logic_group_manager* mgr, dcg_node* node);
static inline int              c_dcg_lgm_exit_node(dcg_logic_group_manager* mgr, dcg_node* node);
static inline int              c_dcg_lgm_label_node(dcg_logic_group_manager* mgr, dcg_node* node);
static inline int              c_dcg_lgm_break_inspection(dcg_logic_group_manager* mgr, dcg_logic_group* group);

// Shelving
static inline int              c_dcg_lgm_shelve(dcg_logic_group_manager* mgr);
static inline int              c_dcg_lgm_unshelve(dcg_logic_group_manager* mgr);

// Queries
static inline dcg_logic_group* c_dcg_lgm_active_group(const dcg_logic_group_manager* mgr);
static inline dcg_node*        c_dcg_lgm_active_node(const dcg_logic_group_manager* mgr);
static inline size_t           c_dcg_lgm_breakpoint_count(const dcg_logic_group_manager* mgr);

// Internal helpers (exposed for reuse and testing - not part of the stable surface)
static inline void*            c_dcg_lgm_reserve(void** block, size_t* capacity, size_t count, size_t element_size, allocator_protocol* allocator);
static inline int              c_dcg_lgm_push_group(dcg_logic_group_manager* mgr, dcg_logic_group* group);
static inline int              c_dcg_lgm_push_node(dcg_logic_group_manager* mgr, dcg_node* node);
static inline int              c_dcg_lgm_push_breakpoint(dcg_logic_group_manager* mgr, dcg_breakpoint_node* breakpoint);
static inline int              c_dcg_lgm_connect_awaiting(dcg_logic_group_manager* mgr, dcg_node* node);

// ========== Utility Functions ==========

/**
 * @brief Stable display name of a group kind.
 *
 * @param lgtype  Group kind.
 * @return Static string; "UNKNOWN" for an out-of-range kind.
 */
static inline const char*      c_dcg_logic_group_type_name(dcg_logic_group_type lgtype) {
    switch (lgtype) {
        case DCG_LG_BASE:
            return "BASE";
        case DCG_LG_MAPPING:
            return "MAPPING";
        default:
            return "UNKNOWN";
    }
}

// ========== Internal Helpers ==========

/**
 * @brief Make room for one more entry in a stack block, growing it by doubling.
 *
 * @param block         Address of the block pointer (updated on growth).
 * @param capacity      Address of the capacity field (updated on growth).
 * @param count         Entries in use.
 * @param element_size  Size of one entry in bytes.
 * @param allocator     Allocator for the grown block (NULL falls back to the heap).
 * @return The block, or NULL on OOM.
 */
static inline void* c_dcg_lgm_reserve(void** block, size_t* capacity, size_t count, size_t element_size, allocator_protocol* allocator) {
    if (count < *capacity) return *block;

    size_t grown_capacity = *capacity ? *capacity * 2 : DCG_LGM_DEFAULT_CAPACITY;
    void*  grown          = c_ap_realloc(*block, grown_capacity * element_size, allocator);
    if (!grown) return NULL;

    *block    = grown;
    *capacity = grown_capacity;
    return grown;
}

/**
 * @brief Push an open group, after setting its parent to the one below it.
 *
 * @param mgr    Manager to modify.
 * @param group  Group being entered.
 * @return DCG_OK or DCG_ERR_OOM.
 */
static inline int c_dcg_lgm_push_group(dcg_logic_group_manager* mgr, dcg_logic_group* group) {
    if (!c_dcg_lgm_reserve((void**) &mgr->groups, &mgr->groups_capacity, mgr->n_groups, sizeof(dcg_logic_group*), mgr->allocator)) return DCG_ERR_OOM;

    group->parent                = mgr->n_groups ? mgr->groups[mgr->n_groups - 1] : NULL;
    mgr->groups[mgr->n_groups++] = group;
    return DCG_OK;
}

/**
 * @brief Push a node being built onto the active-node stack.
 *
 * @param mgr   Manager to modify.
 * @param node  Node being entered.
 * @return DCG_OK or DCG_ERR_OOM.
 */
static inline int c_dcg_lgm_push_node(dcg_logic_group_manager* mgr, dcg_node* node) {
    if (!c_dcg_lgm_reserve((void**) &mgr->nodes, &mgr->nodes_capacity, mgr->n_nodes, sizeof(dcg_node*), mgr->allocator)) return DCG_ERR_OOM;

    mgr->nodes[mgr->n_nodes++] = node;
    return DCG_OK;
}

/**
 * @brief Push a raised breakpoint onto the pending list.
 *
 * @param mgr         Manager to modify.
 * @param breakpoint  Breakpoint that breaks out of a group.
 * @return DCG_OK or DCG_ERR_OOM.
 */
static inline int c_dcg_lgm_push_breakpoint(dcg_logic_group_manager* mgr, dcg_breakpoint_node* breakpoint) {
    if (!c_dcg_lgm_reserve((void**) &mgr->breakpoints, &mgr->breakpoints_capacity, mgr->n_breakpoints, sizeof(dcg_breakpoint_node*), mgr->allocator)) return DCG_ERR_OOM;

    mgr->breakpoints[mgr->n_breakpoints++] = breakpoint;
    return DCG_OK;
}

// ========== Group Lifecycle ==========

/**
 * @brief Initialize a group buf, taking a copy of its name.
 *
 * The buf is populated from scratch, so a reused buf is safe. The name is the
 * one block the base group owns, and it is nested under the group, so the
 * group's free releases it.
 *
 * The buf must live inside an allocator block - the family constructors put it
 * at the head of a larger one - because everything the group owns is nested
 * under it.
 *
 * @param group   Group buf to initialize.
 * @param lgtype  Group kind.
 * @param name    Group name to copy (may be NULL).
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_logic_group_init(dcg_logic_group* group, dcg_logic_group_type lgtype, const char* name) {
    if (!group) return DCG_ERR_INVALID_ARG;

    memset(group, 0, sizeof(*group));
    group->lgtype = lgtype;

    if (name) {
        size_t len  = strlen(name);
        char*  copy = (char*) c_ap_alloc_child(len + 1, NULL, group);
        if (!copy) return DCG_ERR_OOM;
        memcpy(copy, name, len + 1);
        group->name = copy;
    }
    return DCG_OK;
}

/**
 * @brief Allocate a base group: metadata and no store.
 *
 * A scope with values to carry is a dcg_mapping_lgroup, whose constructor
 * initializes this header and then builds its store beside it.
 *
 * @param lgtype     Group kind.
 * @param name       Group name to copy (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The group, or NULL on OOM.
 */
static inline dcg_logic_group* c_dcg_logic_group_new(dcg_logic_group_type lgtype, const char* name, allocator_protocol* allocator) {
    dcg_logic_group* group = (dcg_logic_group*) c_ap_alloc(sizeof(dcg_logic_group), allocator);
    if (!group) return NULL;

    if (c_dcg_logic_group_init(group, lgtype, name) != DCG_OK) {
        c_ap_free_owned(group);
        return NULL;
    }
    return group;
}

/**
 * @brief Tear down a group and free its buf.
 *
 * The name is a nested block of the group, so the free releases it; there is
 * nothing else the base group holds.
 *
 * A variant that adds a store of its own has a _free of its own too: a
 * dcg_mapping_lgroup is released by c_dcg_mapping_lgroup_free(), which hands
 * back its own tables before reaching this one.
 *
 * @param group  Group to free (NULL-safe).
 */
static inline void c_dcg_logic_group_free(dcg_logic_group* group) {
    if (!group) return;
    c_ap_free_owned(group);
}

// ========== Manager Lifecycle ==========

/**
 * @brief Initialize a manager: an empty registry and three empty stacks.
 *
 * @param mgr        Manager to initialize.
 * @param allocator  Allocator for the manager's blocks; NULL uses the plain heap.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_INVALID_BUF.
 */
static inline int c_dcg_lgm_init(dcg_logic_group_manager* mgr, allocator_protocol* allocator) {
    if (!mgr) return DCG_ERR_INVALID_ARG;

    memset(mgr, 0, sizeof(*mgr));
    mgr->allocator = allocator;

    /* One pointer-sized slot per group: the group the name maps to. */
    if (c_bytemap_ex_init(&mgr->registry, DCG_LGM_DEFAULT_CAPACITY, sizeof(uintptr_t), allocator) != BYTEMAP_OK) return DCG_ERR_INVALID_BUF;
    return DCG_OK;
}

/**
 * @brief Release the manager's blocks.
 *
 * The registry table and the three stack blocks go; nothing they point at
 * does. Groups and nodes are the caller's, and freeing a manager that still
 * holds frames is legitimate - a build that is abandoned, or one that is torn
 * down by whoever owns the graph.
 *
 * @param mgr  Manager to free (NULL-safe).
 */
static inline void c_dcg_lgm_free(dcg_logic_group_manager* mgr) {
    if (!mgr) return;

    c_bytemap_ex_dealloc(&mgr->registry);

    for (size_t i = 0; i < mgr->n_shelved; i++) {
        dcg_lgm_state* state = &mgr->shelved[i];
        c_ap_free_owned(state->groups);
        c_ap_free_owned(state->nodes);
        c_ap_free_owned(state->breakpoints);
    }

    c_ap_free_owned(mgr->groups);
    c_ap_free_owned(mgr->nodes);
    c_ap_free_owned(mgr->breakpoints);
    c_ap_free_owned(mgr->shelved);

    memset(mgr, 0, sizeof(*mgr));
}

/**
 * @brief Drop the registry and every open frame, keeping the manager usable.
 *
 * This is the capi's clear(): the caches and the stacks go, the modes stay.
 *
 * @param mgr  Manager to clear (NULL-safe).
 */
static inline void c_dcg_lgm_clear(dcg_logic_group_manager* mgr) {
    if (!mgr) return;

    c_bytemap_ex_clear(&mgr->registry);
    mgr->n_groups      = 0;
    mgr->n_nodes       = 0;
    mgr->n_breakpoints = 0;
    mgr->n_shelved     = 0;
}

// ========== Registry ==========

/**
 * @brief Register a group under its name, so a later build can find it again.
 *
 * The manager borrows the group: it does not own it and does not free it.
 *
 * @param mgr    Manager to modify.
 * @param group  Group to register.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_DUPLICATE when the name is taken.
 */
static inline int c_dcg_lgm_register(dcg_logic_group_manager* mgr, dcg_logic_group* group) {
    if (!mgr || !group || !group->name) return DCG_ERR_INVALID_ARG;
    if (c_bytemap_contains(&mgr->registry, group->name, strlen(group->name)) == BYTEMAP_OK) return DCG_ERR_DUPLICATE;

    if (c_bytemap_set(&mgr->registry, group->name, strlen(group->name), (void*) group, NULL) != BYTEMAP_OK) return DCG_ERR_OOM;
    return DCG_OK;
}

/**
 * @brief Look a group up by name.
 *
 * @param mgr       Manager to read (NULL-safe).
 * @param name      Group name.
 * @param name_len  Length of the name.
 * @return The group, or NULL when the name is not registered.
 */
static inline dcg_logic_group* c_dcg_lgm_find(const dcg_logic_group_manager* mgr, const char* name, size_t name_len) {
    if (!mgr || !name) return NULL;

    dcg_logic_group* found = NULL;
    if (c_bytemap_get(&mgr->registry, name, name_len, (void**) &found) != BYTEMAP_OK) return NULL;
    return found;
}

// ========== Building Inside the Manager ==========

/**
 * @brief Enter a group: it becomes the innermost open one.
 *
 * The group takes the group below it as its parent - the capi's rule, and the
 * reason a nested group can be named by walking up the stack.
 *
 * @param mgr    Manager to modify.
 * @param group  Group being entered.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_lgm_enter_group(dcg_logic_group_manager* mgr, dcg_logic_group* group) {
    if (!mgr || !group) return DCG_ERR_INVALID_ARG;
    return c_dcg_lgm_push_group(mgr, group);
}

/**
 * @brief Leave a group, retiring its breakpoints to be connected later.
 *
 * Leaving is what makes a break real: every breakpoint raised from this group
 * is marked as AWAITING CONNECTION here, and the next node entered picks it up
 * (see c_dcg_lgm_connect_awaiting). A breakpoint that is still awaiting is not
 * an inspection point yet - the branch it resumes into does not exist.
 *
 * Pass the group that is actually open, or NULL to leave the innermost one.
 *
 * @param mgr    Manager to modify.
 * @param group  Group being left, or NULL for the innermost.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_BUSY when another group is open.
 */
static inline int c_dcg_lgm_exit_group(dcg_logic_group_manager* mgr, dcg_logic_group* group) {
    if (!mgr) return DCG_ERR_INVALID_ARG;
    if (!mgr->n_groups) return DCG_ERR_NOT_FOUND;

    dcg_logic_group* current = mgr->groups[mgr->n_groups - 1];
    if (group && current != group) return DCG_ERR_BUSY;

    for (size_t i = 0; i < mgr->n_breakpoints; i++) {
        if (mgr->breakpoints[i]->break_from == current) mgr->breakpoints[i]->await_connection = true;
    }

    mgr->n_groups--;
    return DCG_OK;
}

/**
 * @brief Connect every breakpoint that is waiting, then take the node as active.
 *
 * The two halves of entering a node, in the capi's order:
 *
 *   - a breakpoint that was waiting is CONNECTED to this node: the node becomes
 *     its single child, which is what turns "break out of that group" into
 *     "resume here". A connected breakpoint is retired from the pending list.
 *     The node now has a parent, so the step below adopts it rather than
 *     linking it fresh - the breakpoint goes on naming it (see
 *     c_dcg_node_replace_shared).
 *   - the node then fills the active node's placeholder slot - the branch a
 *     build reserves for whatever comes next - and becomes the active node.
 *
 * An action node is refused: an action is a leaf, so there is nothing to build
 * inside one.
 *
 * @param mgr   Manager to modify.
 * @param node  Node being entered.
 * @return DCG_OK, or a DCG_ERR_* code: DCG_ERR_INVALID_ARG (NULL),
 *         DCG_ERR_TYPE (an action node), DCG_ERR_UNRESOLVED (the active node has
 *         no free edge), DCG_ERR_OOM, or the first failure a connection hit.
 */
static inline int c_dcg_lgm_enter_node(dcg_logic_group_manager* mgr, dcg_node* node) {
    if (!mgr || !node) return DCG_ERR_INVALID_ARG;
    if (c_dcg_node_type_is_action(node->ntype)) return DCG_ERR_TYPE; /* a leaf: nothing is built inside it */

    int ret = c_dcg_lgm_connect_awaiting(mgr, node);
    if (ret != DCG_OK) return ret;

    /* A breakpoint that took the node over is its parent now, and that is what
     * makes the placement below a join rather than a plain link. */
    bool adopted = node->parent != NULL;

    if (mgr->n_nodes) {
        dcg_node* active      = mgr->nodes[mgr->n_nodes - 1];
        dcg_node* placeholder = c_dcg_node_get_placeholder(active);
        if (!placeholder) return DCG_ERR_UNRESOLVED;

        ret = adopted ? c_dcg_node_replace_shared(placeholder, node) : c_dcg_node_replace(placeholder, node);
        if (ret != DCG_OK) return ret;

        c_dcg_node_free_generic(placeholder); /* displaced: the slot is the new node's now */
    }

    return c_dcg_lgm_push_node(mgr, node);
}

/**
 * @brief Connect the awaiting breakpoints to a node and retire them.
 *
 * Walking the list in place, keeping the entries that are not awaiting and the
 * ones a connection refused. A refused connection stays queued rather than
 * being dropped: it is still waiting for a node it can attach to, and the
 * failure is reported to the caller either way.
 *
 * @param mgr   Manager to modify.
 * @param node  Node the breakpoints resume into.
 * @return DCG_OK, or the first failure a connection hit.
 */
static inline int c_dcg_lgm_connect_awaiting(dcg_logic_group_manager* mgr, dcg_node* node) {
    if (!mgr || !node) return DCG_ERR_INVALID_ARG;

    int    error = DCG_OK;
    size_t kept  = 0;

    for (size_t i = 0; i < mgr->n_breakpoints; i++) {
        dcg_breakpoint_node* breakpoint = mgr->breakpoints[i];
        if (!breakpoint->await_connection) {
            mgr->breakpoints[kept++] = breakpoint;
            continue;
        }

        int ret = c_dcg_node_append(&breakpoint->base, node, DCG_NO_CONDITION);
        if (ret != DCG_OK) {
            if (error == DCG_OK) error = ret;
            mgr->breakpoints[kept++] = breakpoint;
            continue;
        }
        breakpoint->await_connection = false;
    }

    mgr->n_breakpoints = kept;
    return error;
}

/**
 * @brief Leave the innermost node being built.
 *
 * @param mgr   Manager to modify.
 * @param node  Node being left.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_NOT_FOUND (no node is open), or
 *         DCG_ERR_BUSY when another node is the active one.
 */
static inline int c_dcg_lgm_exit_node(dcg_logic_group_manager* mgr, dcg_node* node) {
    if (!mgr || !node) return DCG_ERR_INVALID_ARG;
    if (!mgr->n_nodes) return DCG_ERR_NOT_FOUND;
    if (mgr->nodes[mgr->n_nodes - 1] != node) return DCG_ERR_BUSY;

    mgr->n_nodes--;
    return DCG_OK;
}

/**
 * @brief Label a node with the name of every open group.
 *
 * The capi does this when a node is CONSTRUCTED, which is the rule this
 * port keeps: a node belongs to the groups that were open around it, and that
 * does not change when one of them is left. So a builder calls this once per
 * node, right after creating it - and it is deliberately not done by
 * c_dcg_lgm_enter_node(), which runs on every entry and would relabel a node
 * that is merely being descended into.
 *
 * A name already on the node is skipped rather than refused, so calling this
 * twice is harmless.
 *
 * @param mgr   Manager to read.
 * @param node  Node to label.
 * @return The number of labels added, or a DCG_ERR_* code.
 */
static inline int c_dcg_lgm_label_node(dcg_logic_group_manager* mgr, dcg_node* node) {
    if (!mgr || !node) return DCG_ERR_INVALID_ARG;

    int added = 0;
    for (size_t i = 0; i < mgr->n_groups; i++) {
        const char* name = mgr->groups[i]->name;
        if (!name || c_dcg_node_has_label(node, name)) continue;

        int ret = c_dcg_node_add_label(node, name);
        if (ret != DCG_OK) return ret;
        added++;
    }
    return added;
}

/**
 * @brief Break out of a group while a graph is being built.
 *
 * The inspection half of a break, and the only half there is so far. The
 * active node's placeholder slot is swapped for a breakpoint that names the
 * group being broken out of, and the breakpoint is queued: when the group is
 * left it starts waiting for a node to resume into, and the next node entered
 * outside it becomes that node's child.
 *
 * With no active node there is nothing to break out of, and the call is a
 * no-op - the capi's rule, kept because a builder that breaks at the top of a
 * graph is not making an error, it is making an empty branch.
 *
 * @param mgr    Manager to modify.
 * @param group  Group being broken out of.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_UNRESOLVED (the active node has
 *         no slot to break out of) or DCG_ERR_OOM.
 */
static inline int c_dcg_lgm_break_inspection(dcg_logic_group_manager* mgr, dcg_logic_group* group) {
    if (!mgr || !group) return DCG_ERR_INVALID_ARG;
    if (!mgr->n_nodes) return DCG_OK; /* no active node: a break affects nothing */

    dcg_node* active      = mgr->nodes[mgr->n_nodes - 1];
    dcg_node* placeholder = c_dcg_node_get_placeholder(active);
    if (!placeholder) return DCG_ERR_UNRESOLVED;

    dcg_breakpoint_node* breakpoint = c_dcg_node_new_breakpoint(c_ap_protocol_from_ptr(active));
    if (!breakpoint) return DCG_ERR_OOM;

    breakpoint->break_from = group; /* borrowed: the group outlives the graph */

    int ret = c_dcg_node_replace(placeholder, &breakpoint->base);
    if (ret != DCG_OK) {
        c_dcg_node_free_breakpoint(breakpoint);
        return ret;
    }
    c_dcg_node_free_generic(placeholder); /* displaced: the slot is the breakpoint's now */

    ret = c_dcg_lgm_push_breakpoint(mgr, breakpoint);
    if (ret != DCG_OK) return ret; /* the breakpoint stays in the graph, only the queue missed it */
    return DCG_OK;
}

// ========== Shelving ==========

/**
 * @brief Put the open stacks away and start from an empty state.
 *
 * What a sub-graph build needs: the frames of the build around it are handed
 * over whole - not copied - and the manager goes back to holding nothing open.
 * c_dcg_lgm_unshelve() puts the most recently shelved state back, so shelving
 * nests.
 *
 * The modes travel with the state, as they do in the capi.
 *
 * @param mgr  Manager to modify.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_lgm_shelve(dcg_logic_group_manager* mgr) {
    if (!mgr) return DCG_ERR_INVALID_ARG;

    if (!c_dcg_lgm_reserve((void**) &mgr->shelved, &mgr->shelved_capacity, mgr->n_shelved, sizeof(dcg_lgm_state), mgr->allocator)) return DCG_ERR_OOM;

    dcg_lgm_state* state = &mgr->shelved[mgr->n_shelved++];
    memset(state, 0, sizeof(*state));

    /* The blocks move, they are not copied: the manager is left with empty
     * stacks and the sizes it needs are the ones the arrays already have. */
    state->groups               = mgr->groups;
    state->n_groups             = mgr->n_groups;
    state->groups_capacity      = mgr->groups_capacity;
    state->nodes                = mgr->nodes;
    state->n_nodes              = mgr->n_nodes;
    state->nodes_capacity       = mgr->nodes_capacity;
    state->breakpoints          = mgr->breakpoints;
    state->n_breakpoints        = mgr->n_breakpoints;
    state->breakpoints_capacity = mgr->breakpoints_capacity;
    state->inspection_mode      = mgr->inspection_mode;
    state->vigilant_mode        = mgr->vigilant_mode;

    mgr->groups               = NULL;
    mgr->n_groups             = 0;
    mgr->groups_capacity      = 0;
    mgr->nodes                = NULL;
    mgr->n_nodes              = 0;
    mgr->nodes_capacity       = 0;
    mgr->breakpoints          = NULL;
    mgr->n_breakpoints        = 0;
    mgr->breakpoints_capacity = 0;
    return DCG_OK;
}

/**
 * @brief Put the most recently shelved state back, stacks and modes alike.
 *
 * The stacks the manager is holding when this is called are the sub-build's,
 * and this call is what ends it: their blocks go with it. Shelving handed the
 * outer frames to the shelf, so anything on the manager now was opened inside
 * the sub-build - and a frame is bookkeeping, not ownership (the nodes it
 * names belong to the graph), so one that is still open is dropped without
 * leaking anything. That is the capi's behaviour too: it rebinds its lists and
 * lets the discarded ones be collected.
 *
 * @param mgr  Manager to modify.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_NOT_FOUND when nothing is shelved.
 */
static inline int c_dcg_lgm_unshelve(dcg_logic_group_manager* mgr) {
    if (!mgr) return DCG_ERR_INVALID_ARG;
    if (!mgr->n_shelved) return DCG_ERR_NOT_FOUND;

    dcg_lgm_state* state = &mgr->shelved[--mgr->n_shelved];

    c_ap_free_owned(mgr->groups);
    c_ap_free_owned(mgr->nodes);
    c_ap_free_owned(mgr->breakpoints);

    mgr->groups               = state->groups;
    mgr->n_groups             = state->n_groups;
    mgr->groups_capacity      = state->groups_capacity;
    mgr->nodes                = state->nodes;
    mgr->n_nodes              = state->n_nodes;
    mgr->nodes_capacity       = state->nodes_capacity;
    mgr->breakpoints          = state->breakpoints;
    mgr->n_breakpoints        = state->n_breakpoints;
    mgr->breakpoints_capacity = state->breakpoints_capacity;
    mgr->inspection_mode      = state->inspection_mode;
    mgr->vigilant_mode        = state->vigilant_mode;

    memset(state, 0, sizeof(*state));
    return DCG_OK;
}

// ========== Queries ==========

/**
 * @brief The innermost open group, or NULL when none is open.
 *
 * @param mgr  Manager to read (NULL-safe).
 * @return The active group, or NULL.
 */
static inline dcg_logic_group* c_dcg_lgm_active_group(const dcg_logic_group_manager* mgr) {
    if (!mgr || !mgr->n_groups) return NULL;
    return mgr->groups[mgr->n_groups - 1];
}

/**
 * @brief The innermost node being built, or NULL when none is open.
 *
 * @param mgr  Manager to read (NULL-safe).
 * @return The active node, or NULL.
 */
static inline dcg_node* c_dcg_lgm_active_node(const dcg_logic_group_manager* mgr) {
    if (!mgr || !mgr->n_nodes) return NULL;
    return mgr->nodes[mgr->n_nodes - 1];
}

/**
 * @brief Breakpoints raised and not yet connected into the graph.
 *
 * @param mgr  Manager to read (NULL-safe).
 * @return The number of pending breakpoints.
 */
static inline size_t c_dcg_lgm_breakpoint_count(const dcg_logic_group_manager* mgr) {
    if (!mgr) return 0;
    return mgr->n_breakpoints;
}

#endif  // C_DCG_BAKE_LOGIC_GROUP_H
