/*
 * c_collections.h - the mapping logic group: the key index over the value
 * slots, the entries it holds, and the variable read it hands out.
 *
 * A mapping is a GROUP, not a node: it has no children and no kind of its own
 * in the node enum. What it has is a store, and the store is what a build
 * running inside the group reads - so the entries are checked here and the
 * group behaviour they inherit (name, parent, the manager) is checked in
 * test_c_logic_group. The base group carries metadata only; the store is this
 * family's, which is why nothing below overlaps the other suite.
 *
 * The mapping offers no removal and no count of its own: what it holds is read
 * by key, and `n_slots` is the slots in use rather than the number of keys,
 * because two keys may share a slot.
 */

#include <decision_graph/decision_tree/bake/c_collections.h>
#include <decision_graph/decision_tree/bake/c_const.h>

#include <decision_graph/decision_tree/bake/c_action.h>
#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

/* The setters read through a value var, so a literal goes through a local. */
static int map_put(dcg_mapping_lgroup* lgroup, const char* key, dcg_var_t value) {
    return c_dcg_mapping_lgroup_set(lgroup, key, strlen(key), &value);
}

/* The create path answers with a code and an out slot; the key length comes
 * from the text, when there is one to measure. */
static int map_reserve(dcg_mapping_lgroup* lgroup, const char* key, const dcg_var_type* var_type, dcg_var_t** out) {
    return c_dcg_mapping_lgroup_get_create_slot(lgroup, key, key ? strlen(key) : 0, var_type, out);
}

static void test_an_entry_knows_where_it_is(void) {
    /* The create path answers with the entry, and where it is the caller has: the
     * slot points into the store's own block, so the index is what lies between
     * the block and the slot. That index is what a read keeps - an offset into
     * `slots` survives the store growing, where a pointer into it does not. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("idx", 2, NULL);
    dcg_var_t*          slot   = NULL;

    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(lgroup, "a", 1, NULL, &slot), DCG_OK);
    DCG_CHECK_INT((size_t) (slot - lgroup->slots), 0);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(lgroup, "bb", 2, NULL, &slot), DCG_OK);
    DCG_CHECK_INT((size_t) (slot - lgroup->slots), 1);

    /* An entry that is already there answers with the slot it has, not a new one
     * - which is what makes the index a read can keep. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(lgroup, "a", 1, NULL, &slot), DCG_OK);
    DCG_CHECK_INT((size_t) (slot - lgroup->slots), 0);

    /* And the index is where it is THROUGH a growth: the same offset into a block
     * that has been moved, several times over. */
    for (size_t i = 0; i < 8; i++) {
        char key[32];
        (void) snprintf(key, sizeof(key), "grown%zu", i);
        DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(lgroup, key, strlen(key), NULL, &slot), DCG_OK);
    }

    size_t index = 0;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "a", 1, &index), DCG_OK);
    DCG_CHECK_INT(index, 0);
    DCG_CHECK_INT(lgroup->slots[index].dtype, VAR_TYPE_RESERVED); /* the entry, where it moved to */

    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_reservation(void) {
    /* Taking a slot for a key that is new is what the reservation protocol is:
     * the read is built over a slot that has no value yet, and the value lands
     * in it when it arrives. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("store", 0, NULL);
    DCG_CHECK(lgroup != NULL);

    dcg_var_t* slot = NULL;
    DCG_CHECK_INT(map_reserve(lgroup, "close", NULL, &slot), DCG_OK);
    DCG_CHECK(slot != NULL);
    DCG_CHECK(slot == c_dcg_mapping_lgroup_get_slot(lgroup, "close", 5)); /* the entry it now occupies */
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_RESERVED);                        /* no type named: it holds nothing yet */
    DCG_CHECK(c_dcg_var_is_null(slot));                                   /* and reads as absent */
    DCG_CHECK_INT(lgroup->n_slots, 1);

    /* A key that is held hands back its own slot, and is not counted twice. */
    dcg_var_t* held = NULL;
    DCG_CHECK_INT(map_reserve(lgroup, "close", NULL, &held), DCG_OK);
    DCG_CHECK(held == slot);
    DCG_CHECK_INT(lgroup->n_slots, 1);

    /* The type arrives with the value, not with the reservation. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(lgroup, "close", 5, 2.5), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);

#if DCG_MAPPING_IMMUTABLE_DTYPE
    /* The entry keeps the type it was given: a value of another one is refused,
     * and what the slot holds is left exactly as it was. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(lgroup, "close", 5, 7), DCG_ERR_TYPE);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);

    /* The value it does take is one of its own type. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(lgroup, "close", 5, 7.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_double(slot), 7.5);
#else
    /* And the next value replaces it: a slot's type follows what it holds. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(lgroup, "close", 5, 7), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_INT);
    DCG_CHECK_INT(c_dcg_var_as_int(slot), 7);
#endif

    /* Nothing to take a slot from, nothing to take it for, nowhere to put it:
     * each refused with the code that says which, and no slot is handed out. */
    dcg_var_t* out = slot;
    DCG_CHECK_INT(map_reserve(NULL, "x", NULL, &out), DCG_ERR_INVALID_ARG);
    DCG_CHECK(out == NULL);
    DCG_CHECK_INT(map_reserve(lgroup, NULL, NULL, &out), DCG_ERR_INVALID_ARG);
    DCG_CHECK(out == NULL);
    DCG_CHECK_INT(map_reserve(lgroup, "fresh", NULL, NULL), DCG_ERR_INVALID_ARG);

    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_an_entry_born_with_a_type(void) {
    /* A caller that knows what an entry will hold can say so as it is created,
     * and the entry comes into being as that - which is what a build naming the
     * type of an input needs. An entry that is ALREADY there is the store's:
     * asking for a type it does not hold is reported, and the slot it holds is
     * handed back unchanged either way. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("store", 0, NULL);

    dcg_var_type named = VAR_TYPE_INT;
    dcg_var_t*   slot  = NULL;
    DCG_CHECK_INT(map_reserve(lgroup, "size", &named, &slot), DCG_OK);
    DCG_CHECK(slot != NULL);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_INT); /* born as the caller said */

    /* A declared entry is TYPED, not empty: a named type is a promise about
     * what will land, and until it does the slot reads as the zero of that type.
     * The tag is the only field a slot has, so "of this type" and "nothing yet"
     * cannot both be said - naming a type gives up the second. */
    DCG_CHECK(!c_dcg_var_is_null(slot));
    DCG_CHECK_INT(c_dcg_var_as_int(slot), 0);

    /* The same type again: an entry that already is what was asked for. */
    DCG_CHECK_INT(map_reserve(lgroup, "size", &named, &slot), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_INT);

    /* RESERVED and INFERRED claim no type, so neither is ever a mismatch and
     * neither changes an entry that is there. */
    dcg_var_type no_opinion = VAR_TYPE_RESERVED;
    dcg_var_type inferred   = VAR_TYPE_INFERRED;
    DCG_CHECK_INT(map_reserve(lgroup, "size", &no_opinion, &slot), DCG_OK);
    DCG_CHECK_INT(map_reserve(lgroup, "size", &inferred, &slot), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_INT);

#if DCG_MAPPING_IMMUTABLE_DTYPE
    /* The entry keeps the type it was born with: a value of another one is
     * refused, and the slot the caller is handed back is the one the store has. */
    DCG_CHECK_INT(map_put(lgroup, "size", dcg_t_var_double(2.5)), DCG_ERR_TYPE);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_INT);

    /* The caller's idea of the entry is stale either way, and the store says so
     * - in vigilant mode, as a report. */
    DCG_CHECK_INT(map_reserve(lgroup, "size", &named, &slot), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_INT);

    /* What the entry does take is a value of the type it holds. */
    DCG_CHECK_INT(map_put(lgroup, "size", dcg_t_var_int(7)), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(slot), 7);
#else
    /* The value lands, and the type follows the value rather than the asking. */
    DCG_CHECK_INT(map_put(lgroup, "size", dcg_t_var_double(2.5)), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE);

    /* Now the caller's idea of the entry is stale, and the store says so - in
     * vigilant mode, as a report. What is handed back is still its own slot,
     * holding what it holds: the store is not the side being contradicted. */
    DCG_CHECK_INT(map_reserve(lgroup, "size", &named, &slot), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);
#endif

    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_frozen_group(void) {
    /* Freezing seals the store's SHAPE: no entry may come into being, by any
     * path, while what is already held is read and written as before. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("frozen", 0, NULL);
    DCG_CHECK(lgroup != NULL);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(lgroup, "held", 4, 1), DCG_OK);

    lgroup->frozen = true;

    /* A new key is refused, and the code says WHICH way it refused. */
    dcg_var_t* refused = NULL;
    DCG_CHECK_INT(map_reserve(lgroup, "new", NULL, &refused), DCG_ERR_BUSY);
    DCG_CHECK(refused == NULL);                                         /* and no slot is handed out */
    DCG_CHECK(c_dcg_mapping_lgroup_get_slot(lgroup, "new", 3) == NULL); /* nothing was left behind */
    DCG_CHECK_INT(lgroup->n_slots, 1);

    /* A setter cannot sneak one in either: creation is refused wherever it starts. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(lgroup, "sneak", 5, 2), DCG_ERR_BUSY);
    DCG_CHECK_INT(lgroup->n_slots, 1);

    /* What the store holds, it still serves. */
    dcg_var_t* slot = NULL;
    DCG_CHECK_INT(map_reserve(lgroup, "held", NULL, &slot), DCG_OK);
    DCG_CHECK(slot != NULL);
    DCG_CHECK_INT(c_dcg_var_as_int(slot), 1);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(lgroup, "held", 4, 9), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(slot), 9); /* the same live slot */

    /* And thawing gives the shape back. */
    lgroup->frozen = false;
    DCG_CHECK_INT(map_reserve(lgroup, "new", NULL, &slot), DCG_OK);
    DCG_CHECK_INT(lgroup->n_slots, 2);

    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_lifecycle(void) {
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("state", 4, NULL);
    DCG_CHECK(lgroup != NULL);
    DCG_CHECK_INT(lgroup->base.lgtype, DCG_LG_MAPPING);
    DCG_CHECK_STR(lgroup->base.name, "state");
    DCG_CHECK(lgroup->base.parent == NULL);
    DCG_CHECK_INT(lgroup->capacity, 4);
    DCG_CHECK_INT(lgroup->n_slots, 0); /* no slot handed out yet */

    /* The family struct IS the base group, so anything that takes a group takes
     * a mapping - and the store is what the mapping adds to the header. */
    DCG_CHECK((dcg_logic_group*) lgroup == &lgroup->base);
    DCG_CHECK(lgroup->slots != NULL);
    c_dcg_mapping_lgroup_free(lgroup);

    /* A capacity of zero falls back to the default. */
    lgroup = c_dcg_mapping_lgroup_new("state", 0, NULL);
    DCG_CHECK(lgroup != NULL);
    DCG_CHECK_INT(lgroup->capacity, DCG_MAPPING_DEFAULT_CAPACITY);
    c_dcg_mapping_lgroup_free(lgroup);

    (void) printf("    %-26s empty mapping, capacity default %u\n", "new_mapping(0)", (unsigned) DCG_MAPPING_DEFAULT_CAPACITY);

    /* A nameless mapping is legal - a build may use one purely as a store. */
    lgroup = c_dcg_mapping_lgroup_new(NULL, 2, NULL);
    DCG_CHECK(lgroup != NULL);
    DCG_CHECK(lgroup->base.name == NULL);
    c_dcg_mapping_lgroup_free(lgroup);

    c_dcg_mapping_lgroup_free(NULL);
}

static void test_entries(void) {
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("m", 2, NULL);

    DCG_CHECK_INT(map_put(lgroup, "a", dcg_t_var_double(1.5)), DCG_OK);
    DCG_CHECK_INT(map_put(lgroup, "bb", dcg_t_var_int(2)), DCG_OK);
    DCG_CHECK_INT(map_put(lgroup, "ccc", dcg_t_var_string("three")), DCG_OK);
    DCG_CHECK_INT(lgroup->n_slots, 3); /* one slot per key so far */

    /* Reading an entry hands back the INDEX of its slot, and the index is what
     * survives the store growing: the slots block moves, an offset into it does
     * not. A miss is a return code, not a NULL to interpret. */
    size_t index = 0;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "a", 1, &index), DCG_OK);
    DCG_CHECK_INT(index, 0);

    dcg_var_t* slot = &lgroup->slots[index];
    DCG_CHECK(c_dcg_var_as_double(slot) == 1.5);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "bb", 2, &index), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&lgroup->slots[index]), 2);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "ccc", 3, &index), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(&lgroup->slots[index]), "three");

    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "missing", 7, &index), DCG_ERR_NOT_FOUND);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(NULL, "a", 1, &index), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "a", 1, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_slot(lgroup, "a", 1), (intptr_t) slot); /* the key lookup lands on the same slot */

    dcg_t_trace_mapping("mapping(3 keys)", lgroup);

    /* An entry overwrites in place, and the index stays the entry's: nothing a
     * caller wrote down about WHERE the entry is changes when it is written to.
     * What it holds is what may be written to it - "a" holds a double. */
    DCG_CHECK_INT(map_put(lgroup, "a", dcg_t_var_double(99.5)), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "a", 1, &index), DCG_OK);
    DCG_CHECK_INT(index, 0);
    DCG_CHECK(c_dcg_var_as_double(&lgroup->slots[index]) == 99.5);
    DCG_CHECK_INT(lgroup->n_slots, 3); /* an overwrite hands out no new slot */

    dcg_t_trace_mapping("mapping(a overwritten)", lgroup);
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_clear(void) {
    /* Clearing drops the ENTRIES and keeps the store: the block, its capacity
     * and the group around it all stay where they were. That is what makes it
     * safe for a graph whose reads have already resolved - a read holds an
     * OFFSET into this block, so entries emptied in place leave it a valid
     * address, while a block that moved or went would leave it reading freed
     * storage. The owned payload is the clear's to release, which asan checks. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("m", 2, NULL);
    size_t              index  = 0;

    DCG_CHECK_INT(map_put(lgroup, "a", dcg_t_var_double(1.5)), DCG_OK);
    DCG_CHECK_INT(map_put(lgroup, "b", dcg_t_var_string("owned")), DCG_OK);
    DCG_CHECK_INT(lgroup->n_slots, 2);

    dcg_var_t* slots_before = lgroup->slots;
    size_t     capacity     = lgroup->capacity;

    c_dcg_mapping_lgroup_clear(lgroup);

    DCG_CHECK_INT(lgroup->n_slots, 0);
    DCG_CHECK(lgroup->slots == slots_before);   /* emptied in place, not moved */
    DCG_CHECK_INT(lgroup->capacity, capacity);  /* and not shrunk */

    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "a", 1, &index), DCG_ERR_NOT_FOUND);
    DCG_CHECK(c_dcg_mapping_lgroup_get_slot(lgroup, "a", 1) == NULL);

    /* The store is still a store: a key cleared is a new entry again, and a
     * write after the clear lands in the first slot and reads through. */
    DCG_CHECK_INT(map_put(lgroup, "c", dcg_t_var_int(7)), DCG_OK);
    DCG_CHECK_INT(lgroup->n_slots, 1);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_var_idx(lgroup, "c", 1, &index), DCG_OK);
    DCG_CHECK_INT(index, 0);
    DCG_CHECK_INT(c_dcg_var_as_int(&lgroup->slots[index]), 7);
    DCG_CHECK(c_dcg_mapping_lgroup_get_slot(lgroup, "c", 1) == &lgroup->slots[0]);

    /* Clearing what is already empty, and clearing nothing, are both no-ops. */
    c_dcg_mapping_lgroup_clear(lgroup);
    DCG_CHECK_INT(lgroup->n_slots, 0);
    c_dcg_mapping_lgroup_clear(NULL);

    dcg_t_trace_mapping("mapping(cleared)", lgroup);
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_growth(void) {
    /* The slot block grows by doubling, and every earlier entry stays put. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("m", 2, NULL);

    for (int i = 0; i < 20; i++) {
        char key[32];
        (void) snprintf(key, sizeof(key), "key%d", i);
        DCG_CHECK_INT(map_put(lgroup, key, dcg_t_var_int(i)), DCG_OK);
    }
    DCG_CHECK(lgroup->capacity >= 20);
    DCG_CHECK(lgroup->slots != NULL);

    /* Every key still reads back its own value: growth lost nothing. */
    for (int i = 0; i < 20; i++) {
        char key[32];
        (void) snprintf(key, sizeof(key), "key%d", i);
        DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_mapping_lgroup_get_slot(lgroup, key, strlen(key))), i);
    }

    (void) printf("    %-26s 20 keys, slots=%zu, capacity=%zu\n", "growth", lgroup->n_slots, lgroup->capacity);
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_string_values_are_owned(void) {
    char                value[] = "owned";
    dcg_mapping_lgroup* lgroup  = c_dcg_mapping_lgroup_new("m", 2, NULL);

    DCG_CHECK_INT(map_put(lgroup, "k", dcg_t_var_string(value)), DCG_OK);
    value[0] = 'X';

    DCG_CHECK_STR(c_dcg_var_as_string(c_dcg_mapping_lgroup_get_slot(lgroup, "k", 1)), "owned"); /* a copy */

    /* Replacing a string value releases the copy it replaces. */
    DCG_CHECK_INT(map_put(lgroup, "k", dcg_t_var_string("second")), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(c_dcg_mapping_lgroup_get_slot(lgroup, "k", 1)), "second");

    /* And the copy dies with the mapping. */
    dcg_t_trace_mapping("mapping(string value)", lgroup);
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_keys_are_copied(void) {
    /* The bytemap clones keys, so a caller's buffer can be reused at once. */
    char                key[64];
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("m", 4, NULL);

    (void) snprintf(key, sizeof(key), "first");
    DCG_CHECK_INT(map_put(lgroup, key, dcg_t_var_int(1)), DCG_OK);

    (void) snprintf(key, sizeof(key), "second");
    DCG_CHECK_INT(map_put(lgroup, key, dcg_t_var_int(2)), DCG_OK);

    DCG_CHECK(c_dcg_mapping_lgroup_get_slot(lgroup, "first", 5) != NULL);
    DCG_CHECK(c_dcg_mapping_lgroup_get_slot(lgroup, "second", 6) != NULL);
    DCG_CHECK(c_dcg_mapping_lgroup_get_slot(lgroup, "third", 5) == NULL);

    dcg_t_trace_mapping("mapping(reused key buffer)", lgroup);
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_referenced_entries(void) {
    /* An entry can refer to a value the caller owns: the mapping reads it, live,
     * which is how a graph reaches a variable without copying it in. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("m", 4, NULL);
    dcg_var_t           speed;

    (void) c_dcg_var_init_double(&speed, 1.5);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_ref(lgroup, "speed", 5, &speed), DCG_OK);

    dcg_var_t* slot = c_dcg_mapping_lgroup_get_slot(lgroup, "speed", 5);
    DCG_CHECK(slot != NULL);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE_REF);

    (void) c_dcg_var_init_double(&speed, 2.5); /* read time, not set time */
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);
    dcg_t_trace_mapping("mapping(reference)", lgroup);

    /* A value stored over a reference replaces it - when it is a value of the
     * type the reference points at, which is the entry's type either way. */
    dcg_var_t literal;
    (void) c_dcg_var_init_double(&literal, 7.5);
#if DCG_MAPPING_IMMUTABLE_DTYPE
    /* Another type is refused, reference or not: the entry keeps what it holds. */
    dcg_var_t other;
    (void) c_dcg_var_init_int(&other, 7);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set(lgroup, "speed", 5, &other), DCG_ERR_TYPE);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);
#endif
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set(lgroup, "speed", 5, &literal), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_ref(lgroup, "speed", 5, &speed), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);
    dcg_t_trace_mapping("mapping(ref over value)", lgroup);

    /* Storing a value reads the var the caller points at, not the var it keeps. */
    dcg_var_t* held = &literal;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set(lgroup, "held", 4, held), DCG_OK);
    (void) c_dcg_var_init_int(&literal, 8);
    DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_mapping_lgroup_get_slot(lgroup, "held", 4)), 7);

    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_typed_setters(void) {
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("m", 4, NULL);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(lgroup, "d", 1, 0.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(lgroup, "i", 1, -3), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_offset(lgroup, "o", 1, 3), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_bool(lgroup, "b", 1, true), DCG_OK);

    int marker = 0;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_ptr(lgroup, "p", 1, &marker), DCG_OK);

    DCG_CHECK(c_dcg_var_as_double(c_dcg_mapping_lgroup_get_slot(lgroup, "d", 1)) == 0.5);
    DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_mapping_lgroup_get_slot(lgroup, "i", 1)), -3);
    DCG_CHECK_INT(c_dcg_var_as_offset(c_dcg_mapping_lgroup_get_slot(lgroup, "o", 1)), 3);
    DCG_CHECK(c_dcg_var_as_bool(c_dcg_mapping_lgroup_get_slot(lgroup, "b", 1)));
    DCG_CHECK(c_dcg_var_as_ptr(c_dcg_mapping_lgroup_get_slot(lgroup, "p", 1)) == &marker);

    dcg_t_trace_mapping("mapping(typed setters)", lgroup);
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_variable_reads(void) {
    /* A read is how an entry reaches the graph: it holds no value, its slot
     * refers to the entry, and it names the group, the key and the INDEX it read
     * - which is what lets it find the entry again after the store has grown. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("state", 4, NULL);
    DCG_CHECK_INT(map_put(lgroup, "x", dcg_t_var_double(1.0)), DCG_OK);

    dcg_variable_node* var = c_dcg_mapping_lgroup_get_node(lgroup, "x", 1, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->base.ntype, DCG_NODE_VARIABLE);
    DCG_CHECK(var->logic_group == &lgroup->base); /* borrowed: the group owns the value */
    DCG_CHECK_STR(var->key, "x");                 /* the entry it reads */
    DCG_CHECK_STR(var->base.repr, "state.x");     /* the capi's attribute path */

    /* A FREE store moves under a read - it can take another entry, which moves
     * the block every entry lives in, and it can be written a value of another
     * type - so the read is born promising nothing but the entry: its slot holds
     * the entry's OFFSET, tagged as one, and no type of its own. What spends the
     * offset is the read's own evaluation, which is the eval suite's to check. */
    DCG_CHECK_INT(var->base.out.value.as_offset, 0); /* where the entry is, by an index no growth moves */
    DCG_CHECK_INT(var->base.out.dtype, VAR_TYPE_INFERRED); /* the entry, by offset, and no type of its own */
    dcg_t_trace_node("variable(state.x)", &var->base);

    /* A key that is not held has no variable. */
    DCG_CHECK(c_dcg_mapping_lgroup_get_node(lgroup, "nope", 4, NULL) == NULL);
    DCG_CHECK(c_dcg_mapping_lgroup_get_node(NULL, "x", 1, NULL) == NULL);

    c_dcg_node_free_var(var); /* the reflected value stays the group's */
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_a_frozen_stores_read_is_born_unresolved_too(void) {
    /* A frozen store promised not to take another ENTRY - it did not promise that
     * its entries are already answered. So its reads are born exactly like a free
     * store's: holding the entry's offset and no type of their own, which is the
     * state the first evaluation resolves (the eval suite is where that is
     * checked; here it is the birth that is under test).
     *
     * It is not a detail of the tag: an entry that was empty when the store was
     * frozen is not empty forever - a frozen store still lets its entries be
     * written - so a read held at a pointer, or at a type read off the entry at
     * build time, would be an answer given before the question was settled. */
    dcg_mapping_lgroup* lgroup = dcg_t_mapping_lgroup(4, "frozen_born");
    DCG_CHECK_INT(map_put(lgroup, "x", dcg_t_var_double(2.5)), DCG_OK);
    lgroup->frozen = true;

    dcg_variable_node* var = c_dcg_mapping_lgroup_get_node(lgroup, "x", 1, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->base.out.dtype, VAR_TYPE_INFERRED); /* the entry, by offset, and no type yet */
    DCG_CHECK(c_dcg_var_is_null(&var->base.out));          /* and nothing to read from it */
    DCG_CHECK_INT(var->base.out.value.as_offset, 0);       /* where the entry is, for whoever resolves it */
    dcg_t_trace_node("frozen read(x)", &var->base);

    c_dcg_node_free_var(var);
    c_dcg_mapping_lgroup_free(lgroup);
}

static void test_reads_belong_to_the_store(void) {
    /* A read of an entry is a block of the GROUP, so the group releases it -
     * key included - and a caller that takes reads from a store never has to
     * track them. The sanitizer build is what proves the claim: nothing below
     * frees the read, and nothing leaks. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("state", 2, NULL);
    DCG_CHECK_INT(map_put(lgroup, "exposure", dcg_t_var_int(1)), DCG_OK);

    dcg_variable_node* var = c_dcg_mapping_lgroup_get_node(lgroup, "exposure", 8, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK(var->logic_group == &lgroup->base); /* borrowed, and naming the owner */
    DCG_CHECK_STR(var->key, "exposure");          /* the key is the node's own, nested under IT */
    DCG_CHECK_INT(var->base.out.value.as_offset, 0);       /* and it says which entry it reads */
    DCG_CHECK_INT(var->base.out.dtype, VAR_TYPE_INFERRED); /* born naming the entry, nothing more */

    c_dcg_mapping_lgroup_free(lgroup); /* the store, the slot, the read and its key, together */
}

static void test_a_read_can_go_on_its_own(void) {
    /* The other order works too: freeing a read takes it out of the group it
     * belongs to and leaves the store standing. */
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("state", 2, NULL);
    DCG_CHECK_INT(map_put(lgroup, "exposure", dcg_t_var_int(1)), DCG_OK);
    DCG_CHECK_INT(map_put(lgroup, "ttl", dcg_t_var_int(30)), DCG_OK);

    dcg_variable_node* kept = c_dcg_mapping_lgroup_get_node(lgroup, "ttl", 3, NULL);
    dcg_variable_node* gone = c_dcg_mapping_lgroup_get_node(lgroup, "exposure", 8, NULL);

    c_dcg_node_free_var(gone); /* unlinks it from the group's block list */

    /* The store is intact and still handing out reads. */
    DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_mapping_lgroup_get_slot(lgroup, "exposure", 8)), 1);
    DCG_CHECK(c_dcg_mapping_lgroup_get_node(lgroup, "exposure", 8, NULL) != NULL);
    DCG_CHECK_INT(kept->base.out.dtype, VAR_TYPE_INFERRED); /* the entry, by offset: the promise, not the type */

    c_dcg_mapping_lgroup_free(lgroup); /* and `kept` goes with it */
}

static void test_in_the_graph(void) {
    dcg_node*           root   = dcg_t_node_root("Entry Point");
    dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new("state", 2, NULL);
    DCG_CHECK(lgroup != NULL);

    DCG_CHECK_INT(map_put(lgroup, "exposure", dcg_t_var_int(0)), DCG_OK);

    /* A variable read can hang in a graph like any node. The graph owns the
     * READ; the VALUE stays the group's. */
    dcg_variable_node* var = c_dcg_mapping_lgroup_get_node(lgroup, "exposure", 8, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(c_dcg_node_append(root, &var->base, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_subtree_size(root), 2);

    dcg_t_trace_tree("tree(root -> state.exposure)", root);

    /* The read holds no value of its own, and on a free store it does not even
     * hold a type yet: what the graph reads is the entry, and what the read holds
     * is where that entry is. Writing the entry does not move it - it changes what
     * is there to read, which is the eval suite's to check. */
    DCG_CHECK_INT(map_put(lgroup, "exposure", dcg_t_var_int(5)), DCG_OK);
    DCG_CHECK_INT(var->base.out.dtype, VAR_TYPE_INFERRED);

    /* Tearing the graph down frees the two nodes and leaves the store alone. */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 2);
    DCG_CHECK_INT(map_put(lgroup, "exposure", dcg_t_var_int(9)), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_mapping_lgroup_get_slot(lgroup, "exposure", 8)), 9);

    c_dcg_mapping_lgroup_free(lgroup);
}

int main(void) {
    (void) printf("test_c_collection\n");
    DCG_RUN(test_lifecycle);
    DCG_RUN(test_an_entry_knows_where_it_is);
    DCG_RUN(test_reservation);
    DCG_RUN(test_an_entry_born_with_a_type);
    DCG_RUN(test_frozen_group);
    DCG_RUN(test_entries);
    DCG_RUN(test_clear);
    DCG_RUN(test_growth);
    DCG_RUN(test_string_values_are_owned);
    DCG_RUN(test_keys_are_copied);
    DCG_RUN(test_referenced_entries);
    DCG_RUN(test_typed_setters);
    DCG_RUN(test_variable_reads);
    DCG_RUN(test_a_frozen_stores_read_is_born_unresolved_too);
    DCG_RUN(test_reads_belong_to_the_store);
    DCG_RUN(test_a_read_can_go_on_its_own);
    DCG_RUN(test_in_the_graph);
    DCG_SUMMARY("test_c_collection");
    return dcg_test_failures == 0 ? 0 : 1;
}
