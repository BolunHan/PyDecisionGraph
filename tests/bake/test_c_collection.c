/*
 * c_collection.h - the mapping family and the variable node it hands out: the
 * key index over the value slots, the entries it holds, and how a value that
 * another node owns reaches the rest of the graph.
 *
 * The mapping offers no removal and no count of its own: what it holds is read
 * by key, and `n_slots` is the slots in use rather than the number of keys,
 * because two keys may share a slot.
 */

#include <decision_graph/decision_tree/bake/c_collection.h>
#include <decision_graph/decision_tree/bake/c_const.h>

#include <decision_graph/decision_tree/bake/c_action.h>
#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

/* The setters read through a value var, so a literal goes through a local. */
static int map_put(dcg_mapping_node* node, const char* key, dcg_var_t value) {
    return c_dcg_node_set_mapping(node, key, strlen(key), &value);
}

static void test_lifecycle(void) {
    dcg_mapping_node* node = c_dcg_node_new_mapping(4, NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_MAPPING);
    DCG_CHECK_INT(node->capacity, 4);
    DCG_CHECK_INT(node->n_slots, 0); /* no slot handed out yet */

    /* The family struct IS the base node. */
    DCG_CHECK((dcg_node*) node == &node->base);
    c_dcg_node_free_mapping(node);

    /* A capacity of zero falls back to the default. */
    node = c_dcg_node_new_mapping(0, NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->capacity, DCG_MAPPING_DEFAULT_CAPACITY);
    c_dcg_node_free_mapping(node);

    (void) printf("    %-26s empty mapping, capacity default %u\n", "new_mapping(0)", (unsigned) DCG_MAPPING_DEFAULT_CAPACITY);

    /* The mapping carries a payload the base header cannot hold, so the base
     * constructor refuses the kind: only the family knows how big the block is. */
    DCG_CHECK(c_dcg_node_new(DCG_NODE_MAPPING, "m", NULL) == NULL);
    DCG_CHECK(!c_dcg_node_type_is_flat(DCG_NODE_MAPPING));
}

static void test_entries(void) {
    dcg_mapping_node* node = c_dcg_node_new_mapping(2, NULL);

    DCG_CHECK_INT(map_put(node, "a", dcg_t_var_double(1.5)), DCG_OK);
    DCG_CHECK_INT(map_put(node, "bb", dcg_t_var_int(2)), DCG_OK);
    DCG_CHECK_INT(map_put(node, "ccc", dcg_t_var_string("three")), DCG_OK);
    DCG_CHECK_INT(node->n_slots, 3); /* one slot per key so far */

    /* Reading an entry hands back its slot, or NULL when the key is not held -
     * which is also how a caller asks whether the key is there. */
    dcg_var_t* slot = c_dcg_node_get_mapping_var(node, "a", 1);
    DCG_CHECK(slot != NULL);
    DCG_CHECK(c_dcg_var_as_double(slot) == 1.5);
    DCG_CHECK(c_dcg_var_as_int(c_dcg_node_get_mapping_var(node, "bb", 2)) == 2);
    DCG_CHECK_STR(c_dcg_var_as_string(c_dcg_node_get_mapping_var(node, "ccc", 3)), "three");

    DCG_CHECK(c_dcg_node_get_mapping_var(node, "missing", 7) == NULL);
    DCG_CHECK(c_dcg_node_get_mapping_var(NULL, "a", 1) == NULL);

    dcg_t_trace_mapping("mapping(3 keys)", node);

    /* An entry overwrites in place: the key keeps its slot, and a slot a caller
     * is holding keeps reading the entry. */
    DCG_CHECK_INT(map_put(node, "a", dcg_t_var_int(99)), DCG_OK);
    DCG_CHECK(slot == c_dcg_node_get_mapping_var(node, "a", 1)); /* the same slot */
    DCG_CHECK_INT(c_dcg_var_as_int(slot), 99);
    DCG_CHECK_INT(node->n_slots, 3); /* an overwrite hands out no new slot */

    dcg_t_trace_mapping("mapping(a overwritten)", node);
    c_dcg_node_free_mapping(node);
}

static void test_growth(void) {
    /* The slot block grows by doubling, and every earlier entry stays put. */
    dcg_mapping_node* node = c_dcg_node_new_mapping(2, NULL);

    for (int i = 0; i < 20; i++) {
        char key[16];
        (void) snprintf(key, sizeof(key), "key%d", i);
        DCG_CHECK_INT(map_put(node, key, dcg_t_var_int(i)), DCG_OK);
    }
    DCG_CHECK(node->capacity >= 20);
    DCG_CHECK(node->slots != NULL);

    /* Every key still reads back its own value: growth lost nothing. */
    for (int i = 0; i < 20; i++) {
        char key[16];
        (void) snprintf(key, sizeof(key), "key%d", i);
        DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_node_get_mapping_var(node, key, strlen(key))), i);
    }

    (void) printf("    %-26s 20 keys, slots=%zu, capacity=%zu\n", "growth", node->n_slots, node->capacity);
    c_dcg_node_free_mapping(node);
}

static void test_string_values_are_owned(void) {
    char              value[] = "owned";
    dcg_mapping_node* node    = c_dcg_node_new_mapping(2, NULL);

    DCG_CHECK_INT(map_put(node, "k", dcg_t_var_string(value)), DCG_OK);
    value[0] = 'X';

    DCG_CHECK_STR(c_dcg_var_as_string(c_dcg_node_get_mapping_var(node, "k", 1)), "owned"); /* a copy */

    /* Replacing a string value releases the copy it replaces. */
    DCG_CHECK_INT(map_put(node, "k", dcg_t_var_string("second")), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(c_dcg_node_get_mapping_var(node, "k", 1)), "second");

    /* And the copy dies with the mapping. */
    dcg_t_trace_mapping("mapping(string value)", node);
    c_dcg_node_free_mapping(node);
}

static void test_keys_are_copied(void) {
    /* The bytemap clones keys, so a caller's buffer can be reused at once. */
    char              key[64];
    dcg_mapping_node* node = c_dcg_node_new_mapping(4, NULL);

    (void) snprintf(key, sizeof(key), "first");
    DCG_CHECK_INT(map_put(node, key, dcg_t_var_int(1)), DCG_OK);

    (void) snprintf(key, sizeof(key), "second");
    DCG_CHECK_INT(map_put(node, key, dcg_t_var_int(2)), DCG_OK);

    DCG_CHECK(c_dcg_node_get_mapping_var(node, "first", 5) != NULL);
    DCG_CHECK(c_dcg_node_get_mapping_var(node, "second", 6) != NULL);
    DCG_CHECK(c_dcg_node_get_mapping_var(node, "third", 5) == NULL);

    dcg_t_trace_mapping("mapping(reused key buffer)", node);
    c_dcg_node_free_mapping(node);
}

static void test_referenced_entries(void) {
    /* An entry can refer to a value the caller owns: the mapping reads it, live,
     * which is how a graph reaches a variable without copying it in. */
    dcg_mapping_node* node = c_dcg_node_new_mapping(4, NULL);
    dcg_var_t         speed;

    (void) c_dcg_var_init_double(&speed, 1.5);
    DCG_CHECK_INT(c_dcg_node_set_mapping_ref(node, "speed", 5, &speed), DCG_OK);

    dcg_var_t* slot = c_dcg_node_get_mapping_var(node, "speed", 5);
    DCG_CHECK(slot != NULL);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_DOUBLE_REF);

    (void) c_dcg_var_init_double(&speed, 2.5); /* read time, not set time */
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);
    dcg_t_trace_mapping("mapping(reference)", node);

    /* A value stored over a reference replaces it, and a reference over a value
     * releases what the slot held first. */
    dcg_var_t literal;
    (void) c_dcg_var_init_int(&literal, 7);
    DCG_CHECK_INT(c_dcg_node_set_mapping(node, "speed", 5, &literal), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_INT);
    DCG_CHECK_INT(c_dcg_node_set_mapping_ref(node, "speed", 5, &speed), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(slot) == 2.5);
    dcg_t_trace_mapping("mapping(ref over value)", node);

    /* Storing a value reads the var the caller points at, not the var it keeps. */
    dcg_var_t* held = &literal;
    DCG_CHECK_INT(c_dcg_node_set_mapping(node, "held", 4, held), DCG_OK);
    (void) c_dcg_var_init_int(&literal, 8);
    DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_node_get_mapping_var(node, "held", 4)), 7);

    c_dcg_node_free_mapping(node);
}

static void test_typed_setters(void) {
    dcg_mapping_node* node = c_dcg_node_new_mapping(4, NULL);

    DCG_CHECK_INT(c_dcg_node_set_mapping_double(node, "d", 1, 0.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_set_mapping_int(node, "i", 1, -3), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_set_mapping_offset(node, "o", 1, 3), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_set_mapping_bool(node, "b", 1, true), DCG_OK);

    int marker = 0;
    DCG_CHECK_INT(c_dcg_node_set_mapping_ptr(node, "p", 1, &marker), DCG_OK);

    DCG_CHECK(c_dcg_var_as_double(c_dcg_node_get_mapping_var(node, "d", 1)) == 0.5);
    DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_node_get_mapping_var(node, "i", 1)), -3);
    DCG_CHECK_INT(c_dcg_var_as_offset(c_dcg_node_get_mapping_var(node, "o", 1)), 3);
    DCG_CHECK(c_dcg_var_as_bool(c_dcg_node_get_mapping_var(node, "b", 1)));
    DCG_CHECK(c_dcg_var_as_ptr(c_dcg_node_get_mapping_var(node, "p", 1)) == &marker);

    dcg_t_trace_mapping("mapping(typed setters)", node);
    c_dcg_node_free_mapping(node);
}

static void test_variable_nodes(void) {
    /* A variable node is how an entry reaches the graph as a node: it holds no
     * value, its out refers to the entry's slot. */
    dcg_mapping_node* node = c_dcg_node_new_mapping(4, NULL);
    DCG_CHECK_INT(map_put(node, "x", dcg_t_var_double(1.0)), DCG_OK);

    dcg_variable_node* var = c_dcg_node_get_mapping_node(node, "x", 1);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->base.ntype, DCG_NODE_VARIABLE);
    DCG_CHECK(var->parent == &node->base); /* borrowed: the mapping owns the value */
    DCG_CHECK_STR(var->base.repr, "x");    /* the key names the variable */
    DCG_CHECK(c_dcg_var_as_double(&var->base.out) == 1.0);
    dcg_t_trace_node("variable(x)", &var->base);

    /* The node reads the entry live. */
    DCG_CHECK_INT(map_put(node, "x", dcg_t_var_double(9.0)), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&var->base.out) == 9.0);

    /* Its out is a reference to the entry: writing the entry moves what the
     * variable reads, without the node ever holding a value of its own. */
    DCG_CHECK(c_dcg_var_as_ref(&var->base.out) == (const void*) &c_dcg_node_get_mapping_var(node, "x", 1)->value);

    /* A key that is not held has no variable. */
    DCG_CHECK(c_dcg_node_get_mapping_node(node, "nope", 4) == NULL);
    DCG_CHECK(c_dcg_node_get_mapping_node(NULL, "x", 1) == NULL);
    DCG_CHECK(c_dcg_node_new_var("x", NULL, NULL) == NULL);

    c_dcg_node_free_var(var); /* the reflected value stays the mapping's */
    DCG_CHECK_INT(map_put(node, "x", dcg_t_var_int(3)), DCG_OK);
    c_dcg_node_free_mapping(node);
}

static void test_standalone_variable(void) {
    /* A variable can be built over any slot, not just a mapping entry. */
    dcg_var_t slot;
    (void) c_dcg_var_init_int(&slot, 42);

    dcg_variable_node* var = c_dcg_node_new_var("n", &slot, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK(var->parent == NULL);
    DCG_CHECK_INT(c_dcg_var_as_int(&var->base.out), 42);

    (void) c_dcg_var_init_int(&slot, 43);
    DCG_CHECK_INT(c_dcg_var_as_int(&var->base.out), 43);

    c_dcg_node_free_var(var);
    c_dcg_node_free_var(NULL);
}

static void test_list(void) {
    /* A list carries nothing beyond the base node, so the base constructor
     * builds it: its children are the items, in order. */
    dcg_node* list = c_dcg_node_new(DCG_NODE_LIST, "items", NULL);
    DCG_CHECK(list != NULL);
    DCG_CHECK_INT(list->ntype, DCG_NODE_LIST);
    DCG_CHECK_STR(list->repr, "items");
    DCG_CHECK(c_dcg_node_type_is_flat(DCG_NODE_LIST));
    c_dcg_node_free(list);

    /* The variable carries an owner field, so it is not flat either. */
    DCG_CHECK(!c_dcg_node_type_is_flat(DCG_NODE_VARIABLE));
    DCG_CHECK(c_dcg_node_new(DCG_NODE_VARIABLE, "v", NULL) == NULL);
}

static void test_in_the_graph(void) {
    dcg_node*         root = dcg_t_node_root("Entry Point");
    dcg_mapping_node* map  = c_dcg_node_new_mapping(2, NULL);
    DCG_CHECK(map != NULL);
    DCG_CHECK_INT(c_dcg_node_set_repr(&map->base, "map"), DCG_OK);

    DCG_CHECK_INT(map_put(map, "yes", dcg_t_var_bool(true)), DCG_OK);

    /* A mapping is a node: children hang off it like any other. */
    dcg_node* branch = dcg_t_node_double("branch", 1.0);
    DCG_CHECK_INT(c_dcg_node_append(root, &map->base, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(&map->base, branch, DCG_TRUE_CONDITION), DCG_OK);

    DCG_CHECK_INT(c_dcg_node_child_count(&map->base), 1);
    DCG_CHECK_INT(c_dcg_node_subtree_size(root), 3);

    dcg_t_trace_tree("tree(root -> map -> branch)", root);

    /* Every node goes, and the mapping's bytemap goes with it: the walk frees
     * the mapping through its own _free, not the base one. */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3);
}

int main(void) {
    (void) printf("test_c_collection\n");
    DCG_RUN(test_lifecycle);
    DCG_RUN(test_entries);
    DCG_RUN(test_growth);
    DCG_RUN(test_string_values_are_owned);
    DCG_RUN(test_keys_are_copied);
    DCG_RUN(test_referenced_entries);
    DCG_RUN(test_typed_setters);
    DCG_RUN(test_variable_nodes);
    DCG_RUN(test_standalone_variable);
    DCG_RUN(test_list);
    DCG_RUN(test_in_the_graph);
    DCG_SUMMARY("test_c_collection");
    return dcg_test_failures == 0 ? 0 : 1;
}
