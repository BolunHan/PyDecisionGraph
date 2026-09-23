#ifndef C_DCG_BAKE_COLLECTIONS_H
#define C_DCG_BAKE_COLLECTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>
#include <cbase/bytemap/c_bytemap.h>

#include <decision_graph/decision_tree/bake/c_logic_group.h>

/*
 * The collection family: the logic groups that HOLD values rather than compute
 * them. One of them so far, the mapping:
 *
 *   - a mapping is a logic group whose entries are keyed by name, held in a
 *     bytemap index over a flat value array. An entry either holds a value the
 *     mapping owns (a string is copied) or refers to a value the caller owns.
 *
 * That it is a GROUP and not a node is the whole point, and it is the capi's
 * shape: its LogicMapping is a LogicGroup subclass, so what it maps is the
 * store a build runs inside - a node built within the group reads an entry
 * through an attribute access, and the read stays live because the store
 * outlives the graph.
 *
 * A sequence and a generator belong beside it and are not ported: nothing
 * needs them yet, and a family added later costs one struct and its own _free,
 * exactly as this one did.
 */

// ========== Constants ==========

/** Slots a mapping is created with when the caller does not say. */
#ifndef DCG_MAPPING_DEFAULT_CAPACITY
#define DCG_MAPPING_DEFAULT_CAPACITY 8U
#endif

// ========== Structs ==========

/**
 * @brief A mapping logic group: the base group plus a key index over its slots.
 *
 * Two structures, each doing what it is good at:
 *   - `idx_mapping` is a bytemap keyed by the entry name, holding the slot
 *     INDEX as its value. It stores an index rather than a pointer into
 *     `slots`, because `slots` moves when it grows.
 *   - `slots` is the value array itself: n_slots used of capacity, growing by
 *     doubling. Values are contiguous, so a baked lookup is a hash plus an
 *     array index - no pointer chasing.
 *
 * Both are owned by the group, in the two ways there are: the slot block is a
 * nested block of the group - as is the name, as is every variable node built
 * over the store - so the group's own free releases them, while the bytemap
 * keeps its table in a block of its own (see c_bytemap_ex_init), which is why
 * this family has a _free of its own: it hands the table back before the group
 * block goes.
 *
 * The base group must stay the FIRST member: a dcg_mapping_lgroup* is
 * therefore a valid dcg_logic_group*.
 */
typedef struct dcg_mapping_lgroup {
    dcg_logic_group base;         // The common group header. Must stay first.
    bytemap         idx_mapping;  // Entry name -> slot index (stored as a uintptr_t).
    dcg_var_t*      slots;        // OWNED - nested block of `capacity` values.
    size_t          n_slots;      // Slots in use.
    size_t          capacity;     // Slots the block has room for.
    bool            frozen;       // Sealed: no entry may be created in it. Reading and writing what it holds are unaffected.
} dcg_mapping_lgroup;

// ========== Forward Declarations ==========

// Lifecycle
static inline dcg_mapping_lgroup* c_dcg_mapping_lgroup_new(const char* name, size_t capacity, allocator_protocol* allocator);
static inline void                c_dcg_mapping_lgroup_dealloc(dcg_mapping_lgroup* lgroup);
static inline void                c_dcg_mapping_lgroup_free(dcg_mapping_lgroup* lgroup);

// Setter
static inline int                 c_dcg_mapping_lgroup_set(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, dcg_var_t* value);
static inline int                 c_dcg_mapping_lgroup_set_ref(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, dcg_var_t* value);
static inline int                 c_dcg_mapping_lgroup_set_ptr(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, void* value);
static inline int                 c_dcg_mapping_lgroup_set_double(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, double value);
static inline int                 c_dcg_mapping_lgroup_set_int(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, ssize_t value);
static inline int                 c_dcg_mapping_lgroup_set_offset(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, ssize_t value);
static inline int                 c_dcg_mapping_lgroup_set_bool(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, bool value);

// Getter
static inline int                 c_dcg_mapping_lgroup_get_var_idx(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, size_t* out_index);
static inline dcg_variable_node*  c_dcg_mapping_lgroup_get_node(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, allocator_protocol* allocator);

// Evaluation - what a read of an entry evaluates to (see c_eval.h)
static inline int                 c_dcg_node_mapping_var_node_eval_hook(dcg_node* header, void* user_data);

// Internal helpers (exposed for reuse and testing - not part of the stable surface)
static inline dcg_var_t*          c_dcg_mapping_lgroup_get_slot(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len);
static inline int                 c_dcg_mapping_lgroup_get_create_slot(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, const dcg_var_type* var_type, dcg_var_t** out);

// ========== Evaluation ==========

/**
 * @brief What a read of an entry evaluates to: the store's answer, once.
 *
 * The slot says which of the two states the read is in, and the store's promise
 * is not part of it - a read of a frozen store resolves exactly like a read of a
 * free one, and for the same reason: an entry either holds a value yet or it does
 * not, and only the entry can say which:
 *
 *   - VAR_TYPE_INFERRED (or VAR_TYPE_OFFSET, the same state in the other tag):
 *     unresolved. The slot holds the entry's offset, which the store handed out
 *     and never retires - it has no pop - so the entry is that many slots into the
 *     block. This is where the offset is spent, and the first evaluation is the
 *     last one that finds this state;
 *   - anything else: the reference the read resolved to. It is the fast path, and
 *     it is where every evaluation after the first lands.
 *
 * Called for a read the store BUILT. A variable built by hand, or one reading a
 * group that is not a mapping, carries none of these states and is answered by
 * the dispatch instead (see c_dcg_node_eval_default).
 *
 * @param header     The read to evaluate.
 * @param user_data  Unused: the hooks share one signature.
 * @return DCG_OK when the read is answered, DCG_ERR_UNBOUND when its entry holds
 *         nothing yet, or a DCG_ERR_* from the mint.
 */
static inline int                 c_dcg_node_mapping_var_node_eval_hook(dcg_node* header, void* user_data) {
    (void) user_data;

    dcg_var_t*   slot = &header->out;

    /* Unresolved, or already the reference the resolution left: the fast path is
     * every evaluation after the first, and it is one tag test wide. */
    dcg_var_type dtype = slot->dtype;
    if (dtype != VAR_TYPE_INFERRED && dtype != VAR_TYPE_OFFSET) return DCG_OK;

    /* The offset is the payload, read as the member it was written as and not
     * through c_dcg_var_as_offset: that reader follows the base tag, and an
     * INFERRED base is RESERVED, which no reader resolves - the tag that makes
     * the slot inert is the same one that keeps the offset out of reach of it. */
    dcg_mapping_lgroup* mapping = (dcg_mapping_lgroup*) ((dcg_variable_node*) header)->logic_group;
    dcg_var_t*          entry   = &mapping->slots[slot->value.as_offset];
    if (entry->dtype == VAR_TYPE_RESERVED) return DCG_ERR_UNBOUND; /* the entry holds nothing yet */

    /* The resolution, and it happens once: the offset becomes a reference to the
     * entry it named - the reference every later evaluation takes the fast path
     * on, until the store is written another value into that entry. */
    return c_dcg_var_init_ref(slot, entry);
}

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate a mapping group with room for `capacity` entries.
 *
 * The base group header is initialized first - a mapping is a group - and the
 * entry index and slot block follow.
 *
 * @param name       Group name to copy (may be NULL).
 * @param capacity   Initial slot capacity (clamped to DCG_MAPPING_DEFAULT_CAPACITY if 0).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The group, or NULL on OOM.
 */
static inline dcg_mapping_lgroup* c_dcg_mapping_lgroup_new(const char* name, size_t capacity, allocator_protocol* allocator) {
    if (capacity == 0) capacity = DCG_MAPPING_DEFAULT_CAPACITY;

    dcg_mapping_lgroup* lgroup = (dcg_mapping_lgroup*) c_ap_alloc(sizeof(dcg_mapping_lgroup), allocator);
    if (!lgroup) return NULL;

    if (c_dcg_logic_group_init(&lgroup->base, DCG_LG_MAPPING, name) != DCG_OK) {
        c_ap_free_owned(lgroup);
        return NULL;
    }

    /* One pointer-sized value slot per entry: the index of the value in `slots`. */
    if (c_bytemap_ex_init(&lgroup->idx_mapping, capacity, sizeof(uintptr_t), c_ap_protocol_from_ptr(lgroup)) != BYTEMAP_OK) {
        c_ap_free_owned(lgroup);
        return NULL;
    }

    lgroup->slots = (dcg_var_t*) c_ap_alloc_child(capacity * sizeof(dcg_var_t), NULL, lgroup);
    if (!lgroup->slots) {
        c_bytemap_ex_dealloc(&lgroup->idx_mapping); /* the table is a block of its own, not a child */
        c_ap_free_owned(lgroup);
        return NULL;
    }

    for (size_t i = 0; i < capacity; i++) (void) c_dcg_var_init(&lgroup->slots[i]);
    lgroup->n_slots  = 0;
    lgroup->capacity = capacity;
    return lgroup;
}

/**
 * @brief Tear down a mapping's contents, leaving the group buf alone.
 *
 * Two things go, in this order: the entry index, whose table is a block of its
 * own and is handed back first (while the field that names it is still intact),
 * and then the base group's half, which releases the name. The entry slots, the
 * string copies of the entries and every variable node built over the store are
 * nested blocks of the group block, so they go with it rather than here.
 *
 * @param lgroup  Group to tear down (NULL-safe).
 */
static inline void c_dcg_mapping_lgroup_dealloc(dcg_mapping_lgroup* lgroup) {
    if (!lgroup) return;
    c_bytemap_ex_dealloc(&lgroup->idx_mapping); /* a table block of its own, not a child */
    c_dcg_logic_group_dealloc(&lgroup->base);
}

/**
 * @brief Tear down a mapping group and free its buf.
 *
 * The clean half first, then the block.
 *
 * @param lgroup  Group to free (NULL-safe).
 */
static inline void c_dcg_mapping_lgroup_free(dcg_mapping_lgroup* lgroup) {
    if (!lgroup) return;
    c_dcg_mapping_lgroup_dealloc(lgroup);
    c_ap_free_owned(lgroup);
}

// ========== Internal Helpers ==========

/**
 * @brief Get a slot: the one an entry occupies, or NULL when the key is not held.
 *
 * @param lgroup   Group to read (NULL-safe).
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @return The live slot, or NULL.
 */
/**
 * @brief Find the entry a key names, as the INDEX of its slot in the store.
 *
 * The index, not a pointer to the slot, and the difference is the whole reason
 * this exists: `slots` is a block that GROWS, and growing it moves every entry
 * in it. A pointer taken today is wrong the moment the store takes one more
 * entry; an index is an offset into whatever block the store has now, so it
 * outlives the growth - which is what lets a read built over an entry keep
 * reading it after the store has grown around it.
 *
 * @param lgroup      Group to read (NULL-safe).
 * @param key         Entry name.
 * @param key_len     Length of key.
 * @param out_index   Receives the index of the entry's slot.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, or DCG_ERR_NOT_FOUND (no such entry).
 */
static inline int c_dcg_mapping_lgroup_get_var_idx(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, size_t* out_index) {
    if (out_index) *out_index = 0;
    if (!lgroup || !key || !out_index) return DCG_ERR_INVALID_ARG;

    void* stored = NULL;
    if (c_bytemap_get(&lgroup->idx_mapping, key, key_len, &stored) != BYTEMAP_OK) return DCG_ERR_NOT_FOUND;

    size_t index = (size_t) (uintptr_t) stored;
    if (index >= lgroup->n_slots) return DCG_ERR_NOT_FOUND;

    *out_index = index;
    return DCG_OK;
}

static inline dcg_var_t* c_dcg_mapping_lgroup_get_slot(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len) {
    size_t index = 0;
    if (c_dcg_mapping_lgroup_get_var_idx(lgroup, key, key_len, &index) != DCG_OK) return NULL;
    return (dcg_var_t*) &lgroup->slots[index]; /* the index came out of the table, so the block holds it */
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
 * A FROZEN group creates nothing NEW. This is the single point where an entry
 * comes into being, so the check sits exactly there and every path - a setter, a
 * reservation - inherits it, while an entry the group already holds is still
 * served by both. It is the store's SHAPE that is sealed, not its contents, and
 * the code says which way it went: DCG_ERR_BUSY for the store that said no,
 * DCG_ERR_OOM for a machine that ran out.
 *
 * A NEW entry takes the type the caller names in `var_type`, or comes into
 * being RESERVED when the caller names none - a slot with room for a value and
 * no opinion yet about what it will hold, which is what a read of an entry that
 * is not written yet needs. An entry that is ALREADY there keeps the type it
 * has: the caller's type is checked against it in vigilant mode and the
 * mismatch is reported - never asserted, since the store is not the side being
 * contradicted - and RESERVED and INFERRED mean "no type claimed", so neither is
 * ever a mismatch. What the entry holds is the truth either way.
 *
 * @param lgroup    Group to modify.
 * @param key       Entry name.
 * @param key_len   Length of key.
 * @param var_type  Type the caller wants a NEW entry to have (NULL, RESERVED and
 *                  INFERRED all mean "no type named"). Never written to.
 * @param out       Receives the slot: the live slot on DCG_OK, NULL on any failure.
 *                  WHERE it is, the caller has: the block and the slot are both in
 *                  front of it, and a subtraction is the index.
 * @return DCG_OK; DCG_ERR_INVALID_ARG for a NULL argument; DCG_ERR_BUSY for a new
 *         key in a frozen group; DCG_ERR_OOM; DCG_ERR_RANGE for an index this
 *         mapping never handed out.
 */
static inline int c_dcg_mapping_lgroup_get_create_slot(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, const dcg_var_type* var_type, dcg_var_t** out) {
    /* The out slot is cleared first, so a caller that ignores the code still
     * cannot read a slot out of a call that did not take one. */
    if (!out) return DCG_ERR_INVALID_ARG;
    *out = NULL;

    if (!lgroup || !key) return DCG_ERR_INVALID_ARG;

    uintptr_t index      = (uintptr_t) lgroup->n_slots;
    char*     stored     = NULL;
    size_t    stored_len = 0;

    if (c_bytemap_ex_set_default(&lgroup->idx_mapping, key, key_len, (const char*) &index, sizeof(index), 0, &stored, &stored_len) != BYTEMAP_OK) return DCG_ERR_OOM;
    if (!stored || stored_len != sizeof(uintptr_t)) return DCG_ERR_OOM;

    uintptr_t slot_index = 0;
    memcpy(&slot_index, stored, sizeof(slot_index));

    /* The key was new exactly when the map handed back the default, which is the
     * index of a slot this block does not have yet. */
    if (slot_index == lgroup->n_slots) {
        if (lgroup->frozen) {
            c_bytemap_pop(&lgroup->idx_mapping, key, key_len, NULL); /* take the index back */
            return DCG_ERR_BUSY;                                     /* sealed: nothing new appears here */
        }

        if (lgroup->n_slots == lgroup->capacity) {
            size_t new_capacity = lgroup->capacity ? lgroup->capacity * 2 : DCG_MAPPING_DEFAULT_CAPACITY;
            void*  grown        = c_ap_realloc(lgroup->slots, new_capacity * sizeof(dcg_var_t), NULL);
            if (!grown) {
                c_bytemap_pop(&lgroup->idx_mapping, key, key_len, NULL); /* take the index back */
                return DCG_ERR_OOM;
            }

            lgroup->slots = (dcg_var_t*) grown;
            for (size_t i = lgroup->capacity; i < new_capacity; i++) (void) c_dcg_var_init(&lgroup->slots[i]);
            lgroup->capacity = new_capacity;
        }

        (void) c_dcg_var_init_reserved(&lgroup->slots[slot_index]); /* born holding nothing */
        if (var_type && *var_type != VAR_TYPE_RESERVED && *var_type != VAR_TYPE_INFERRED) lgroup->slots[slot_index].dtype = *var_type;

        lgroup->n_slots++;
    }
    else if (var_type && *var_type != VAR_TYPE_RESERVED && *var_type != VAR_TYPE_INFERRED) {
        /* The entry is already there and the caller named a type for it: if it
         * holds another one, the caller is working from a wrong idea of the
         * store and is told so. Reported rather than refused - the slot is what
         * the store has and it is handed back as it is. */
#if DCG_VIGILANT
        if (lgroup->slots[slot_index].dtype != *var_type) {
            (void) fprintf(
                stderr, "[DCG] c_dcg_mapping_lgroup_get_create_slot: entry \"%.*s\" is %s, not the %s the caller named - the entry keeps what it holds\n", (int) key_len,
                key, c_dcg_var_type_name(lgroup->slots[slot_index].dtype), c_dcg_var_type_name(*var_type)
            );
            (void) fflush(stderr);
        }
#endif
    }

    if (slot_index >= lgroup->n_slots) return DCG_ERR_RANGE; /* not an index this mapping handed out */
    *out = &lgroup->slots[slot_index];
    return DCG_OK;
}

// ========== Public APIs - Setters ==========

/**
 * @brief Store the value another var holds under a key.
 *
 * The value is what is stored, not the var: a string is copied in, a scalar or
 * pointer is taken as it is, and what the caller does with its own var
 * afterwards is its own business. Use c_dcg_mapping_lgroup_set_ref() when the
 * entry should follow that var instead.
 *
 * @param lgroup   Group to modify.
 * @param key      Entry name.
 * @param key_len  Length of key (excluding any NUL).
 * @param value    Value to store (read now).
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_RANGE or DCG_ERR_OOM.
 */
static inline int c_dcg_mapping_lgroup_set(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, dcg_var_t* value) {
    if (!lgroup || !key || !value) return DCG_ERR_INVALID_ARG;

    dcg_var_t* slot     = NULL;
    int        ret_code = c_dcg_mapping_lgroup_get_create_slot(lgroup, key, key_len, NULL, &slot);
    if (ret_code != DCG_OK) return ret_code;

    /* What the slot held goes first: a string value is a nested block of this
     * group, and it is about to be replaced. */
    if (slot->dtype == VAR_TYPE_STRING && slot->value.as_string) {
        c_ap_free_owned((void*) slot->value.as_string);
        (void) c_dcg_var_init(slot);
    }

    if (value->dtype != VAR_TYPE_STRING) {
        *slot = *value;
        return DCG_OK;
    }
    if (!value->value.as_string) return c_dcg_var_init_string(slot, NULL);

    /* The text is copied into a block nested under this group, so the caller's
     * string can go away at once. */
    size_t len  = strlen(value->value.as_string);
    char*  copy = (char*) c_ap_alloc_child(len + 1, NULL, lgroup);
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
 * @param lgroup   Group to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to refer to (must outlive the mapping).
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_mapping_lgroup_set_ref(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, dcg_var_t* value) {
    if (!lgroup || !key || !value) return DCG_ERR_INVALID_ARG;

    dcg_var_t* slot     = NULL;
    int        ret_code = c_dcg_mapping_lgroup_get_create_slot(lgroup, key, key_len, NULL, &slot);
    if (ret_code != DCG_OK) return ret_code;

    if (slot->dtype == VAR_TYPE_STRING && slot->value.as_string) c_ap_free_owned((void*) slot->value.as_string);
    return c_dcg_var_init_ref(slot, value);
}

/**
 * @brief Store a raw pointer under a key.
 *
 * @param lgroup   Group to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Pointer to store (not owned).
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_mapping_lgroup_set_ptr(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, void* value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_ptr(&packed, value);
    return c_dcg_mapping_lgroup_set(lgroup, key, key_len, &packed);
}

/**
 * @brief Store a double under a key.
 *
 * @param lgroup   Group to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_mapping_lgroup_set_double(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, double value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_double(&packed, value);
    return c_dcg_mapping_lgroup_set(lgroup, key, key_len, &packed);
}

/**
 * @brief Store an integer under a key.
 *
 * @param lgroup   Group to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_mapping_lgroup_set_int(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, ssize_t value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_int(&packed, value);
    return c_dcg_mapping_lgroup_set(lgroup, key, key_len, &packed);
}

/**
 * @brief Store an offset under a key.
 *
 * @param lgroup   Group to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_mapping_lgroup_set_offset(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, ssize_t value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_offset(&packed, value);
    return c_dcg_mapping_lgroup_set(lgroup, key, key_len, &packed);
}

/**
 * @brief Store a boolean under a key.
 *
 * @param lgroup   Group to modify.
 * @param key      Entry name.
 * @param key_len  Length of key.
 * @param value    Value to store.
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_OOM.
 */
static inline int c_dcg_mapping_lgroup_set_bool(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, bool value) {
    dcg_var_t packed;
    (void) c_dcg_var_init_bool(&packed, value);
    return c_dcg_mapping_lgroup_set(lgroup, key, key_len, &packed);
}

// ========== Public APIs - Getters ==========

/**
 * @brief A variable node reading an entry, or NULL when the key is not held.
 *
 * The node is allocated for the caller, which frees it with
 * c_dcg_node_free_var(); its `logic_group` records this group and its `key` the
 * name - and WHERE the entry is, it names in its own out slot.
 *
 * What the node is BORN with there is the same for every store: the entry's
 * OFFSET, tagged VAR_TYPE_INFERRED - "the entry's type, whatever it is when you
 * read me". What the read is resolved TO is not decided here and not by the
 * store's `frozen`: it is decided by the entry, the first time the read is
 * evaluated (see c_dcg_node_mapping_var_node_eval_hook). An entry a store holds
 * nothing in, and an entry written after the read was built, are the same
 * question with the same answer - and holding the read at a pointer into the
 * block would answer a different one, because `slots` moves when the store
 * grows, and a frozen store is a store that stops growing, not a store whose
 * reads are already answered.
 *
 * From that resolution on, the slot is a reference INTO the store's block, so
 * that block has to stay where it is. A store is not grown under a graph that
 * reads it - the bake pass is what will hold it to that, and `frozen` is what a
 * caller holds it to now.
 *
 * The repr is the attribute path the capi gives an AttrExpression -
 * `group.key` - so a rendering names the entry the way the build did.
 *
 * The group is taken NON-const on purpose, and it is the one place in this
 * file that does: the node this hands out reads the store live, long after the
 * call returns, so the binding is to a store that is expected to move. Handing
 * one out from a group the caller promised not to change would be a promise
 * this call cannot keep on someone else's behalf.
 *
 * @param lgroup     Group to read (NULL-safe).
 * @param key        Entry name.
 * @param key_len    Length of key.
 * @param allocator  Allocator for the node; NULL derives it from the group.
 * @return The variable node, or NULL when the key is missing / on OOM.
 */
static inline dcg_variable_node* c_dcg_mapping_lgroup_get_node(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, allocator_protocol* allocator) {
    dcg_var_t* slot = c_dcg_mapping_lgroup_get_slot(lgroup, key, key_len);
    if (!slot) return NULL;

    char repr[DCG_NODE_STRING_MAXLEN];
    if (lgroup->base.name) (void) snprintf(repr, sizeof(repr), "%s.%.*s", lgroup->base.name, (int) key_len, key);
    else (void) snprintf(repr, sizeof(repr), "%.*s", (int) key_len, key);

    dcg_variable_node* node = c_dcg_node_new_var(repr, key, key_len, NULL, &lgroup->base, allocator);
    if (!node) return NULL;

    /* The read's beginning, and the same one for every store: where the entry is,
     * and no type of its own. The offset is the payload, written as the member
     * the rule reads it from. */
    node->base.out.dtype           = VAR_TYPE_INFERRED;
    node->base.out.value.as_offset = (ssize_t) (slot - lgroup->slots);

#if DCG_EVAL_DIRECT_HOOKS
    /* A read made BY a store carries the store's own rule: what the entry holds
     * now IS what the node reads, and the store is what knows it. A variable node
     * built by hand - bound to a slot, with no store to look anything up in -
     * carries no hook and is evaluated by the dispatch instead (see
     * c_dcg_node_eval_default). */
    node->base.eval_ctx.type_eval_fn = c_dcg_node_mapping_var_node_eval_hook;
#endif
    return node;
}

#endif  // C_DCG_BAKE_COLLECTIONS_H