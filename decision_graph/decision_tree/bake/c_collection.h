#ifndef C_DCG_BAKE_COLLECTION_H
#define C_DCG_BAKE_COLLECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>
#include <cbase/bytemap/c_bytemap.h>

#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The collection family: the nodes that hold other values instead of computing
 * one. Two of them:
 *
 *   - a mapping: entries keyed by name, held in a bytemap index over a flat
 *     value array. An entry either holds a value the mapping owns (a string is
 *     copied) or refers to a value the caller owns;
 *   - a variable: a node with no value of its own, whose out refers to a value
 *     another node holds. It is what lets a mapping entry - or a future
 *     collection's item - be handed to the rest of the graph as a node.
 */

// ========== Constants ==========

/** Slots a mapping is created with when the caller does not say. */
#ifndef DCG_MAPPING_DEFAULT_CAPACITY
#define DCG_MAPPING_DEFAULT_CAPACITY 8U
#endif

// ========== Structs ==========

/**
 * @brief A mapping node: the base node plus a key index over its value slots.
 *
 * Two structures, each doing what it is good at:
 *   - `idx_mapping` is a bytemap keyed by the entry name, holding the slot
 *     INDEX as its value. It stores an index rather than a pointer into
 *     `slots`, because `slots` moves when it grows.
 *   - `slots` is the value array itself: n_slots used of capacity, growing by
 *     doubling. Values are contiguous, so a baked lookup is a hash plus an
 *     array index - no pointer chasing.
 *
 * Both are owned by the node: the bytemap and the slot block are nested under
 * it, so freeing the node releases the whole mapping.
 *
 * The base node must stay the FIRST member: a dcg_mapping_node* is therefore a
 * valid dcg_node*.
 */
typedef struct dcg_mapping_node {
    dcg_node   base;         // The common node header. Must stay first.
    bytemap    idx_mapping;  // Entry name -> slot index (stored as a uintptr_t).
    dcg_var_t* slots;        // OWNED - nested block of `capacity` values.
    size_t     n_slots;      // Slots in use.
    size_t     capacity;     // Slots the block has room for.
} dcg_mapping_node;

/**
 * @brief A variable node: a node that reflects a value another node holds.
 *
 * It carries no value of its own - base.out is a REFERENCE to the value slot
 * it was built over, so reading the variable reads that slot, live. That is
 * what makes it usable as an input to an expression (c_expr.h binds the out
 * slot of its inputs) while the value itself stays where it belongs.
 *
 * `parent` names the node the value belongs to, when the caller knows it (a
 * mapping entry knows its mapping); it is a borrowed back-pointer for
 * inspection, never an owner, and it does not keep the value alive.
 *
 * The base node must stay the FIRST member: a dcg_variable_node* is therefore
 * a valid dcg_node*.
 */
typedef struct dcg_variable_node {
    dcg_node  base;    // The common node header. Must stay first.
    dcg_node* parent;  // NOT owned - the node that holds the reflected value, or NULL.
} dcg_variable_node;

// ========== Forward Declarations ==========

// Lifecycle
static inline dcg_mapping_node*  c_dcg_node_new_mapping(size_t capacity, allocator_protocol* allocator);
static inline void               c_dcg_node_free_mapping(dcg_mapping_node* node);

// Setter
static inline int                c_dcg_node_set_mapping(dcg_mapping_node* node, const char* key, size_t key_len, dcg_var_t* value);
static inline int                c_dcg_node_set_mapping_ref(dcg_mapping_node* node, const char* key, size_t key_len, dcg_var_t* value);
static inline int                c_dcg_node_set_mapping_ptr(dcg_mapping_node* node, const char* key, size_t key_len, void* value);
static inline int                c_dcg_node_set_mapping_double(dcg_mapping_node* node, const char* key, size_t key_len, double value);
static inline int                c_dcg_node_set_mapping_int(dcg_mapping_node* node, const char* key, size_t key_len, ssize_t value);
static inline int                c_dcg_node_set_mapping_offset(dcg_mapping_node* node, const char* key, size_t key_len, ssize_t value);
static inline int                c_dcg_node_set_mapping_bool(dcg_mapping_node* node, const char* key, size_t key_len, bool value);

// Getter
static inline dcg_var_t*         c_dcg_node_get_mapping_var(const dcg_mapping_node* node, const char* key, size_t key_len);
static inline dcg_variable_node* c_dcg_node_get_mapping_node(const dcg_mapping_node* node, const char* key, size_t key_len);

// Variable node
static inline dcg_variable_node* c_dcg_node_new_var(const char* repr, dcg_var_t* value, allocator_protocol* allocator);
static inline void               c_dcg_node_free_var(dcg_variable_node* node);

// Payload teardown, registered with the base by the mapping constructor
static inline void               c_dcg_node_mapping_variant_dealloc(dcg_node* node);

// Internal helpers (exposed for reuse and testing - not part of the stable surface)
static inline dcg_var_t*         c_dcg_node_mapping_get_slot(const dcg_mapping_node* node, const char* key, size_t key_len);
static inline dcg_var_t*         c_dcg_node_mapping_get_create_slot(dcg_mapping_node* node, const char* key, size_t key_len);

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate a mapping node with room for `capacity` entries.
 *
 * @param capacity   Initial slot capacity (clamped to DCG_MAPPING_DEFAULT_CAPACITY if 0).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_mapping_node*  c_dcg_node_new_mapping(size_t capacity, allocator_protocol* allocator) {
    if (capacity == 0) capacity = DCG_MAPPING_DEFAULT_CAPACITY;

    dcg_mapping_node* node = (dcg_mapping_node*) c_ap_alloc(sizeof(dcg_mapping_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, DCG_NODE_MAPPING, NULL) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    /* One pointer-sized value slot per entry: the index of the value in `slots`. */
    if (c_bytemap_ex_init(&node->idx_mapping, capacity, sizeof(uintptr_t), c_ap_protocol_from_ptr(node)) != BYTEMAP_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    node->slots = (dcg_var_t*) c_ap_alloc_child(capacity * sizeof(dcg_var_t), NULL, node);
    if (!node->slots) {
        c_bytemap_ex_dealloc(&node->idx_mapping); /* the table is a block of its own, not a child */
        c_ap_free_owned(node);
        return NULL;
    }

    for (size_t i = 0; i < capacity; i++) (void) c_dcg_var_init(&node->slots[i]);
    node->n_slots  = 0;
    node->capacity = capacity;

    /* The base cannot see the index or the slots; this is how it releases them. */
    node->base.fn_variant_dealloc = c_dcg_node_mapping_variant_dealloc;
    return node;
}

/**
 * @brief Release the mapping payload - the base variant hook.
 *
 * The entries come out first (a string value is a nested block, and the bytemap
 * holds a table of its own), then the base teardown frees the node.
 *
 * @param node  The node being torn down (a dcg_node* that is really the variant).
 */
static inline void c_dcg_node_mapping_variant_dealloc(dcg_node* node) {
    dcg_mapping_node* mapping = (dcg_mapping_node*) node;

    for (size_t i = 0; i < mapping->n_slots; i++) {
        if (mapping->slots && mapping->slots[i].dtype == VAR_TYPE_STRING && mapping->slots[i].value.as_string) {
            c_ap_free_owned((void*) mapping->slots[i].value.as_string);
        }
    }
    c_bytemap_ex_dealloc(&mapping->idx_mapping);
    if (mapping->slots) c_ap_free_owned(mapping->slots);

    mapping->slots    = NULL;
    mapping->n_slots  = 0;
    mapping->capacity = 0;
}

/**
 * @brief Tear down a mapping node and free its buf.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_mapping(dcg_mapping_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base); /* runs the variant hook, then the base teardown */
}

// ========== Internal Helpers ==========

/**
 * @brief Get a slot: the one an entry occupies, or NULL when the key is not held.
 *
 * @param node     Node to read (NULL-safe).
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @return The live slot, or NULL.
 */
static inline dcg_var_t* c_dcg_node_mapping_get_slot(const dcg_mapping_node* node, const char* key, size_t key_len) {
    if (!node || !key) return NULL;

    void* stored = NULL;
    if (c_bytemap_get(&node->idx_mapping, key, key_len, &stored) != BYTEMAP_OK) return NULL;

    size_t index = (size_t) (uintptr_t) stored;
    if (index >= node->n_slots) return NULL;
    return &node->slots[index];
}

/**
 * @brief Get or create a slot: the one an entry occupies, taking one when the
 * key is new.
 *
 * One hash and one probe walk, whether the key is known or new: the index the
 * mapping would hand out next is offered to the map as the default, and
 * c_bytemap_ex_set_default either finds the key and hands back its own index or
 * writes that default. A get followed by a set would hash the key twice and walk
 * the table twice for the same answer.
 *
 * The slot block grows by doubling, and only when the key really was new; the
 * bytemap holds the slot INDEX, so a move does not invalidate the index - which
 * is why the index, rather than a pointer into the block, is what the map holds.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @return The slot, or NULL on OOM / invalid argument.
 */
static inline dcg_var_t* c_dcg_node_mapping_get_create_slot(dcg_mapping_node* node, const char* key, size_t key_len) {
    if (!node || !key) return NULL;

    uintptr_t index      = (uintptr_t) node->n_slots;
    char*     stored     = NULL;
    size_t    stored_len = 0;

    if (c_bytemap_ex_set_default(&node->idx_mapping, key, key_len, (const char*) &index, sizeof(index), 0, &stored, &stored_len) != BYTEMAP_OK) return NULL;
    if (!stored || stored_len != sizeof(uintptr_t)) return NULL;

    uintptr_t slot_index = 0;
    memcpy(&slot_index, stored, sizeof(slot_index));

    /* The key was new exactly when the map handed back the default, which is the
     * index of a slot this block does not have yet. */
    if (slot_index == node->n_slots) {
        if (node->n_slots == node->capacity) {
            size_t new_capacity = node->capacity ? node->capacity * 2 : DCG_MAPPING_DEFAULT_CAPACITY;
            void*  grown        = c_ap_realloc(node->slots, new_capacity * sizeof(dcg_var_t), NULL);
            if (!grown) {
                c_bytemap_pop(&node->idx_mapping, key, key_len, NULL); /* take the index back */
                return NULL;
            }

            node->slots = (dcg_var_t*) grown;
            for (size_t i = node->capacity; i < new_capacity; i++) (void) c_dcg_var_init(&node->slots[i]);
            node->capacity = new_capacity;
        }
        node->n_slots++;
    }

    if (slot_index >= node->n_slots) return NULL; /* not an index this mapping handed out */
    return &node->slots[slot_index];
}

// ========== Public APIs - Setters ==========

/**
 * @brief Store the value another var holds under a key.
 *
 * The value is what is stored, not the var: a string is copied in, a scalar or
 * pointer is taken as it is, and what the caller does with its own var
 * afterwards is its own business. Use c_dcg_node_set_mapping_ref() when the
 * entry should follow that var instead.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key (excluding any NUL).
 * @param value    Value to store (read now).
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_RANGE or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_mapping(dcg_mapping_node* node, const char* key, size_t key_len, dcg_var_t* value) {
    if (!node || !key || !value) return DCG_ERR_INVALID_ARG;

    dcg_var_t* slot = c_dcg_node_mapping_get_create_slot(node, key, key_len);
    if (!slot) return DCG_ERR_OOM;

    /* What the slot held goes first: a string value is a nested block of this
     * node, and it is about to be replaced. */
    if (slot->dtype == VAR_TYPE_STRING && slot->value.as_string) {
        c_ap_free_owned((void*) slot->value.as_string);
        (void) c_dcg_var_init(slot);
    }

    if (value->dtype != VAR_TYPE_STRING) {
        *slot = *value;
        return DCG_OK;
    }
    if (!value->value.as_string) return c_dcg_var_init_string(slot, NULL);

    /* The text is copied into a block nested under this node, so the caller's
     * string can go away at once. */
    size_t len  = strlen(value->value.as_string);
    char*  copy = (char*) c_ap_alloc_child(len + 1, NULL, node);
    if (!copy) return DCG_ERR_OOM;
    memcpy(copy, value->value.as_string, len + 1);
    return c_dcg_var_init_string(slot, copy);
}

/**
 * @brief Store a REFERENCE to another var under a key.
 *
 * The entry reads what that var holds, at the moment it is read, for as long as
 * the var outlives the mapping - which is how a mapping entry points at a
 * caller's variable without copying it.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to refer to (must outlive the mapping).
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_mapping_ref(dcg_mapping_node* node, const char* key, size_t key_len, dcg_var_t* value) {
    if (!node || !key || !value) return DCG_ERR_INVALID_ARG;

    dcg_var_t* slot = c_dcg_node_mapping_get_create_slot(node, key, key_len);
    if (!slot) return DCG_ERR_OOM;

    if (slot->dtype == VAR_TYPE_STRING && slot->value.as_string) c_ap_free_owned((void*) slot->value.as_string);
    return c_dcg_var_init_ref(slot, value);
}

/**
 * @brief Store a raw pointer under a key.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Pointer to store (not owned).
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_mapping_ptr(dcg_mapping_node* node, const char* key, size_t key_len, void* value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_ptr(&packed, value);
    return c_dcg_node_set_mapping(node, key, key_len, &packed);
}

/**
 * @brief Store a double under a key.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_mapping_double(dcg_mapping_node* node, const char* key, size_t key_len, double value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_double(&packed, value);
    return c_dcg_node_set_mapping(node, key, key_len, &packed);
}

/**
 * @brief Store an integer under a key.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_mapping_int(dcg_mapping_node* node, const char* key, size_t key_len, ssize_t value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_int(&packed, value);
    return c_dcg_node_set_mapping(node, key, key_len, &packed);
}

/**
 * @brief Store an offset under a key.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_mapping_offset(dcg_mapping_node* node, const char* key, size_t key_len, ssize_t value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_offset(&packed, value);
    return c_dcg_node_set_mapping(node, key, key_len, &packed);
}

/**
 * @brief Store a boolean under a key.
 *
 * @param node     Node to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_node_set_mapping_bool(dcg_mapping_node* node, const char* key, size_t key_len, bool value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_bool(&packed, value);
    return c_dcg_node_set_mapping(node, key, key_len, &packed);
}

// ========== Public APIs - Getters ==========

/**
 * @brief The value slot an entry occupies, or NULL when the key is not held.
 *
 * This is the live slot, not a copy: it is what a caller refers to (see
 * c_dcg_var_init_ref) and what a variable node is built over. It stays valid
 * for as long as the key is held - an entry that is overwritten keeps its slot,
 * and only an entry that is removed retires it.
 *
 * @param node     Node to read (NULL-safe).
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @return The slot, or NULL.
 */
static inline dcg_var_t* c_dcg_node_get_mapping_var(const dcg_mapping_node* node, const char* key, size_t key_len) {
    return c_dcg_node_mapping_get_slot(node, key, key_len);
}

/**
 * @brief A variable node reflecting an entry, or NULL when the key is not held.
 *
 * The node is allocated for the caller, which frees it with
 * c_dcg_node_free_var(); its `parent` records the mapping it came from, and its
 * out refers to the entry's slot, so evaluating the graph reads the entry of
 * the moment.
 *
 * @param node     Node to read (NULL-safe).
 * @param key      Entry name (the variable's repr).
 * @param key_len  Length of key.
 * @return The variable node, or NULL when the key is missing / on OOM.
 */
static inline dcg_variable_node* c_dcg_node_get_mapping_node(const dcg_mapping_node* node, const char* key, size_t key_len) {
    dcg_var_t* slot = c_dcg_node_mapping_get_slot(node, key, key_len);
    if (!slot) return NULL;

    dcg_variable_node* var = c_dcg_node_new_var(key, slot, node ? c_ap_protocol_from_ptr((void*) node) : NULL);
    if (var) var->parent = (dcg_node*) (const void*) node; /* borrowed: the mapping owns the value */
    return var;
}

// ========== Public APIs - The Variable Node ==========

/**
 * @brief Allocate a node that reflects a value another node holds.
 *
 * The node owns nothing: its out is a reference to `value`, so the node reads
 * that value live and hands it to whoever reads the node - an expression bound
 * to it, a caller inspecting the graph. `value` must outlive the node.
 *
 * @param repr       Display text to copy (may be NULL).
 * @param value      Value slot to reflect (must outlive the node).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / a NULL value.
 */
static inline dcg_variable_node* c_dcg_node_new_var(const char* repr, dcg_var_t* value, allocator_protocol* allocator) {
    if (!value) return NULL;

    dcg_variable_node* node = (dcg_variable_node*) c_ap_alloc(sizeof(dcg_variable_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, DCG_NODE_VARIABLE, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    node->parent = NULL;
    (void) c_dcg_var_init_ref(&node->base.out, value);
    return node;
}

/**
 * @brief Tear down a variable node and free its buf.
 *
 * The reflected value is the caller's, so there is nothing else to release.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_var(dcg_variable_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base);
}

#endif  // C_DCG_BAKE_COLLECTION_H
