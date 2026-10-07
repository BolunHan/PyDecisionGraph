/*
 * c_node.h / c_hierarchy.h - hierarchy: graph building rules, branch
 * inference, traversal queries, labels, validation, and the teardown that
 * frees a whole graph from its top.
 */

#include <decision_graph/decision_tree/bake/c_node.h>

#include <decision_graph/decision_tree/bake/c_collections.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_expr.h>

#include <decision_graph/decision_tree/bake/c_action.h>

#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

/*
 * A helper for the invalid states a builder cannot reach through the API:
 * it links a child the way a hand-written bake table would, skipping every
 * rule, so validation can be tested against the structure it must reject.
 */
static void force_link(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition) {
    dcg_node* tail             = parent->children;
    child->parent              = parent;
    child->condition_to_parent = condition;
    child->next_sibling        = NULL;
    if (!tail) {
        child->prev_sibling = NULL;
        parent->children    = child;
        return;
    }
    while (tail->next_sibling) tail = tail->next_sibling;
    tail->next_sibling  = child;
    child->prev_sibling = tail;
}

/* ------------------------------------------------------------------ */
/* Building                                                            */
/* ------------------------------------------------------------------ */

static void test_append_and_order(void) {
    dcg_node* parent = dcg_t_node_plain("list");
    dcg_node* first  = dcg_t_node_double("first", 1.0);
    dcg_node* second = dcg_t_node_double("second", 2.0);
    dcg_node* third  = dcg_t_node_double("third", 3.0);

    DCG_CHECK_INT(c_dcg_node_append(parent, first, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(parent, second, DCG_NO_CONDITION), DCG_ERR_DUPLICATE);
    DCG_CHECK_INT(c_dcg_node_append(parent, second, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(parent, third, DCG_FALSE_CONDITION), DCG_OK);

    /* Children keep the order they were appended in. */
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 3);
    DCG_CHECK(c_dcg_node_first_child(parent) == first);
    DCG_CHECK(c_dcg_node_last_child(parent) == third);
    DCG_CHECK(c_dcg_node_child_at(parent, 0) == first);
    DCG_CHECK(c_dcg_node_child_at(parent, 2) == third);
    DCG_CHECK(c_dcg_node_child_at(parent, 3) == NULL);

    DCG_CHECK_INT(c_dcg_node_child_index(first), 0);
    DCG_CHECK_INT(c_dcg_node_child_index(second), 1);
    DCG_CHECK_INT(c_dcg_node_child_index(third), 2);

    DCG_CHECK(c_dcg_node_next_sibling(first) == second);
    DCG_CHECK(c_dcg_node_prev_sibling(third) == second);
    DCG_CHECK(c_dcg_node_next_sibling(third) == NULL);
    DCG_CHECK(c_dcg_node_prev_sibling(first) == NULL);

    /* A child knows its edge and its parent. */
    DCG_CHECK(first->parent == parent);
    DCG_CHECK(c_dcg_condition_is_true(second->condition_to_parent));
    DCG_CHECK(c_dcg_condition_is_false(third->condition_to_parent));

    /* Lookups by edge condition. */
    DCG_CHECK(c_dcg_node_child_by_condition(parent, DCG_TRUE_CONDITION) == second);
    DCG_CHECK(c_dcg_node_child_by_condition(parent, DCG_ELSE_CONDITION) == NULL);
    DCG_CHECK(c_dcg_node_child_by_condition(parent, DCG_FALSE_CONDITION) == third);
    DCG_CHECK(c_dcg_node_child_by_condition(parent, DCG_ELSE_CONDITION) == NULL);

    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 4); /* parent, first, second, third */
}

static void test_append_rejections(void) {
    dcg_node* parent = dcg_t_node_plain("parent");
    dcg_node* child  = dcg_t_node_double("child", 1.0);
    dcg_node* other  = dcg_t_node_double("other", 2.0);

    DCG_CHECK_INT(c_dcg_node_append(NULL, child, DCG_NO_CONDITION), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_append(parent, NULL, DCG_NO_CONDITION), DCG_ERR_INVALID_ARG);
    /* The capi refuses to guess an edge, and so does the port. */
    DCG_CHECK_INT(c_dcg_node_append(parent, child, NULL), DCG_ERR_INVALID_ARG);
    /* A node cannot be its own child. */
    DCG_CHECK_INT(c_dcg_node_append(parent, parent, DCG_NO_CONDITION), DCG_ERR_CYCLE);

    DCG_CHECK_INT(c_dcg_node_append(parent, child, DCG_NO_CONDITION), DCG_OK);
    /* A child that already has a parent must be detached first. */
    DCG_CHECK_INT(c_dcg_node_append(other, child, DCG_NO_CONDITION), DCG_ERR_BUSY);
    /* The parent is not its own descendant. */
    DCG_CHECK_INT(c_dcg_node_append(child, parent, DCG_NO_CONDITION), DCG_ERR_CYCLE);
    /* A deeper cycle is refused too. */
    DCG_CHECK_INT(c_dcg_node_append(other, child, DCG_NO_CONDITION), DCG_ERR_BUSY);

    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 2); /* parent + child */
    c_dcg_node_free(other);
}

static void test_else_rules(void) {
    dcg_node* parent   = dcg_t_node_plain("parent");
    dcg_node* first    = dcg_t_node_double("first", 1.0);
    dcg_node* fallback = dcg_t_node_double("fallback", 0.0);
    dcg_node* too_late = dcg_t_node_double("too late", 9.0);

    DCG_CHECK_INT(c_dcg_node_append(parent, first, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(parent, fallback, DCG_ELSE_CONDITION), DCG_OK);

    /* An else branch is the fallback: nothing may follow it (that branch
     * would be unreachable), and there may only ever be one of them. */
    DCG_CHECK_INT(c_dcg_node_append(parent, too_late, DCG_FALSE_CONDITION), DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_append(parent, too_late, DCG_ELSE_CONDITION), DCG_ERR_DUPLICATE);
    c_dcg_node_free(too_late);
    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 3); /* parent, first, fallback */

    /* An else branch can never be inserted in front of another child... */
    parent   = dcg_t_node_plain("parent");
    first    = dcg_t_node_double("first", 1.0);
    fallback = dcg_t_node_double("fallback", 0.0);
    too_late = dcg_t_node_double("too late", 9.0);
    DCG_CHECK_INT(c_dcg_node_append(parent, first, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_auto(parent, fallback), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_at(parent, too_late, DCG_ELSE_CONDITION, 0), DCG_ERR_EDGE);

    /* ...while inserting a regular branch in front of a trailing else is
     * fine: the else is still last when the dust settles. */
    DCG_CHECK_INT(c_dcg_node_append(parent, too_late, DCG_ELSE_CONDITION), DCG_OK);
    DCG_CHECK(c_dcg_node_last_child(parent) == too_late);
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 3);

    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 4); /* parent, first, fallback, too_late */
}

static void test_root_rules(void) {
    dcg_node* root   = dcg_t_node_root("Entry Point");
    dcg_node* entry  = dcg_t_node_binary(DCG_OP_LT, "x < 10");
    dcg_node* second = dcg_t_node_double("second", 1.0);

    /* A root takes exactly one unconditioned child... */
    DCG_CHECK_INT(c_dcg_node_append(root, entry, DCG_TRUE_CONDITION), DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_append(root, entry, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(root), 1);
    DCG_CHECK(c_dcg_condition_is_none(entry->condition_to_parent));

    /* ...and no second one. */
    DCG_CHECK_INT(c_dcg_node_append(root, second, DCG_NO_CONDITION), DCG_ERR_TYPE);

    /* AUTO normalizes to the unconditional edge under a root. */
    c_dcg_node_free_generic(entry); /* an expression releases what it holds, and only the dispatcher knows it does */
    DCG_CHECK_INT(c_dcg_node_append(root, second, DCG_AUTO_CONDITION), DCG_OK);
    DCG_CHECK(c_dcg_condition_is_none(second->condition_to_parent));

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 2); /* root + second */
}

static void test_inference(void) {
    dcg_node* parent = dcg_t_node_plain("parent");
    dcg_node* first  = dcg_t_node_double("first", 1.0);
    dcg_node* second = dcg_t_node_double("second", 2.0);
    dcg_node* third  = dcg_t_node_double("third", 3.0);

    /* No children yet: the first branch is always TRUE. */
    DCG_CHECK(c_dcg_node_infer_condition(parent) == DCG_TRUE_CONDITION);
    DCG_CHECK_INT(c_dcg_node_append_auto(parent, first), DCG_OK);
    DCG_CHECK(c_dcg_condition_is_true(first->condition_to_parent));

    /* One binary branch: infer the opposite. */
    DCG_CHECK(c_dcg_node_infer_condition(parent) == DCG_FALSE_CONDITION);
    DCG_CHECK_INT(c_dcg_node_append_auto(parent, second), DCG_OK);
    DCG_CHECK(c_dcg_condition_is_false(second->condition_to_parent));

    /* Two explicit binary branches: nothing left to infer. */
    DCG_CHECK(c_dcg_node_infer_condition(parent) == NULL);
    DCG_CHECK_INT(c_dcg_node_append_auto(parent, third), DCG_ERR_UNRESOLVED);

    c_dcg_node_free(third);
    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 3); /* parent, first, second */

    /* A single auto-generated else branch infers TRUE... */
    parent                = dcg_t_node_plain("parent");
    dcg_node* placeholder = dcg_t_node_placeholder("holder");
    DCG_CHECK_INT(c_dcg_node_append(parent, placeholder, DCG_ELSE_CONDITION), DCG_OK);
    DCG_CHECK(c_dcg_node_infer_condition(parent) == DCG_TRUE_CONDITION);

    /* ...but a hand-written one cannot be completed automatically. */
    placeholder->autogen = false;
    DCG_CHECK(c_dcg_node_infer_condition(parent) == NULL);
    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 2); /* parent + placeholder */
}

static void test_autogen_edge_reuse(void) {
    /* With two or more branches the newest auto-generated edge is handed
     * back, so the builder can locate and replace the slot it created. */
    dcg_node* parent      = dcg_t_node_plain("parent");
    dcg_node* first       = dcg_t_node_double("first", 1.0);
    dcg_node* auto_filled = dcg_t_node_action(DCG_NODE_NOACTION, "auto");

    DCG_CHECK_INT(c_dcg_node_append(parent, first, DCG_TRUE_CONDITION), DCG_OK);
    auto_filled->autogen = true;
    DCG_CHECK_INT(c_dcg_node_append(parent, auto_filled, DCG_ELSE_CONDITION), DCG_OK);

    DCG_CHECK(c_dcg_node_infer_condition(parent) == DCG_ELSE_CONDITION);
    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 3); /* parent, first, auto_filled */
}

static void test_insert_at(void) {
    dcg_node* parent = dcg_t_node_plain("parent");
    dcg_node* first  = dcg_t_node_double("first", 1.0);
    dcg_node* third  = dcg_t_node_double("third", 3.0);
    dcg_node* middle = dcg_t_node_double("middle", 2.0);

    DCG_CHECK_INT(c_dcg_node_append(parent, first, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(parent, third, DCG_FALSE_CONDITION), DCG_OK);

    /* An index at the tail is a plain append. */
    dcg_node* last = dcg_t_node_double("last", 4.0);
    DCG_CHECK_INT(c_dcg_node_append_at(parent, last, DCG_ELSE_CONDITION, 99), DCG_OK);
    DCG_CHECK(c_dcg_node_last_child(parent) == last);

    /* Inserting in the middle keeps every link consistent. */
    DCG_CHECK_INT(c_dcg_node_append_at(parent, middle, DCG_NO_CONDITION, 1), DCG_OK);
    DCG_CHECK(c_dcg_node_child_at(parent, 1) == middle);
    DCG_CHECK(c_dcg_node_next_sibling(first) == middle);
    DCG_CHECK(c_dcg_node_prev_sibling(third) == middle);
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 4);

    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 5); /* parent + the four children */
}

static void test_detach_replace_remove(void) {
    dcg_node* parent      = dcg_t_node_plain("parent");
    dcg_node* first       = dcg_t_node_double("first", 1.0);
    dcg_node* second      = dcg_t_node_double("second", 2.0);
    dcg_node* replacement = dcg_t_node_double("replacement", 9.0);

    DCG_CHECK_INT(c_dcg_node_append(parent, first, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(parent, second, DCG_FALSE_CONDITION), DCG_OK);

    /* Detach: the node survives with its subtree, its edge is dropped. */
    DCG_CHECK_INT(c_dcg_node_detach(first), DCG_OK);
    DCG_CHECK(first->parent == NULL);
    DCG_CHECK(c_dcg_condition_is_none(first->condition_to_parent));
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 1);
    DCG_CHECK_INT(c_dcg_node_detach(first), DCG_OK); /* already detached: a no-op */

    /* Replace: the slot - position and edge - is inherited. */
    DCG_CHECK_INT(c_dcg_node_replace(second, replacement), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 1);
    DCG_CHECK(c_dcg_node_first_child(parent) == replacement);
    DCG_CHECK(c_dcg_condition_is_false(replacement->condition_to_parent));
    DCG_CHECK(second->parent == NULL); /* displaced, but still alive */

    /* A detached node has no slot to replace. */
    dcg_node* orphan = dcg_t_node_double("orphan", 0.0);
    DCG_CHECK_INT(c_dcg_node_replace(orphan, first), DCG_ERR_INVALID_ARG);
    c_dcg_node_free_generic(orphan); /* the same, for a node no graph holds */

    c_dcg_node_free(first);
    c_dcg_node_free(second);
    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 2); /* parent + replacement */
}

static void test_clear_children(void) {
    dcg_node* parent     = dcg_t_node_plain("parent");
    dcg_node* child      = dcg_t_node_plain("child");
    dcg_node* grandchild = dcg_t_node_double("grandchild", 1.0);

    DCG_CHECK_INT(c_dcg_node_append(parent, child, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_auto(child, grandchild), DCG_OK);

    DCG_CHECK_INT(c_dcg_node_clear_children(parent), 2); /* the child and its grandchild */
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 0);
    DCG_CHECK(c_dcg_node_is_leaf(parent));

    DCG_CHECK_INT(c_dcg_node_subtree_size(parent), 1);
    c_dcg_node_free(parent);
}

static void test_frozen_parent(void) {
    dcg_node* parent = dcg_t_node_plain("parent");
    dcg_node* child  = dcg_t_node_double("child", 1.0);

    parent->flags |= DCG_NODE_FLAG_FROZEN;
    DCG_CHECK_INT(c_dcg_node_append(parent, child, DCG_NO_CONDITION), DCG_ERR_BUSY);

    /* Unfreezing is the only way back in - a baked graph is immutable. */
    parent->flags &= ~(uint32_t) DCG_NODE_FLAG_FROZEN;
    DCG_CHECK_INT(c_dcg_node_append(parent, child, DCG_NO_CONDITION), DCG_OK);
    parent->flags |= DCG_NODE_FLAG_FROZEN;
    DCG_CHECK_INT(c_dcg_node_detach(child), DCG_ERR_BUSY);

    /* Teardown still unlinks: a frozen parent must not pin a dead child. */
    c_dcg_node_free(child);
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 0);
    c_dcg_node_free(parent);
}

/* ------------------------------------------------------------------ */
/* Traversal queries                                                   */
/* ------------------------------------------------------------------ */

/* root -> branch -> {yes, no}, plus a breakpoint leaf under branch. */
static dcg_node* build_sample_tree(dcg_node** out_branch, dcg_node** out_yes, dcg_node** out_no) {
    dcg_node* root   = dcg_t_node_root("Entry Point");
    dcg_node* branch = dcg_t_node_binary(DCG_OP_LT, "x < 10");
    dcg_node* yes    = dcg_t_node_action(DCG_NODE_LONGACTION, "go long");
    dcg_node* no     = dcg_t_node_action(DCG_NODE_SHORTACTION, "go short");

    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    (void) c_dcg_node_append_auto(branch, yes);
    (void) c_dcg_node_append_auto(branch, no);

    if (out_branch) *out_branch = branch;
    if (out_yes) *out_yes = yes;
    if (out_no) *out_no = no;
    return root;
}

static void test_traversal_queries(void) {
    dcg_node* branch = NULL;
    dcg_node* yes    = NULL;
    dcg_node* no     = NULL;
    dcg_node* root   = build_sample_tree(&branch, &yes, &no);

    DCG_CHECK(c_dcg_node_root(yes) == root);
    DCG_CHECK(c_dcg_node_root(root) == root);
    DCG_CHECK_INT(c_dcg_node_depth(root), 0);
    DCG_CHECK_INT(c_dcg_node_depth(branch), 1);
    DCG_CHECK_INT(c_dcg_node_depth(yes), 2);
    DCG_CHECK_INT(c_dcg_node_height(root), 2);
    DCG_CHECK_INT(c_dcg_node_height(branch), 1);
    DCG_CHECK_INT(c_dcg_node_height(yes), 0);

    DCG_CHECK_INT(c_dcg_node_subtree_size(root), 4);
    DCG_CHECK_INT(c_dcg_node_subtree_size(branch), 3);
    DCG_CHECK_INT(c_dcg_node_leaf_count(root), 2);
    DCG_CHECK_INT(c_dcg_node_leaf_count(branch), 2);

    DCG_CHECK(!c_dcg_node_is_leaf(root));
    DCG_CHECK(c_dcg_node_is_leaf(yes));
    DCG_CHECK(c_dcg_node_is_root(root));
    DCG_CHECK(!c_dcg_node_is_root(yes));

    DCG_CHECK(c_dcg_node_is_ancestor(root, yes));
    DCG_CHECK(c_dcg_node_is_ancestor(branch, no));
    DCG_CHECK(!c_dcg_node_is_ancestor(yes, no));
    DCG_CHECK(!c_dcg_node_is_ancestor(yes, yes));

    /* Depth-first, left to right: yes before no. */
    dcg_node* leaves[4] = {NULL, NULL, NULL, NULL};
    DCG_CHECK_INT(c_dcg_node_collect_leaves(root, leaves, 4), 2);
    DCG_CHECK(leaves[0] == yes);
    DCG_CHECK(leaves[1] == no);

    /* Counting works without a destination array. */
    DCG_CHECK_INT(c_dcg_node_collect_leaves(root, NULL, 0), 2);

    /* A capped array counts everything but stores what fits. */
    dcg_node* one_slot[1] = {NULL};
    DCG_CHECK_INT(c_dcg_node_collect_leaves(root, one_slot, 1), 2);
    DCG_CHECK(one_slot[0] == yes);

    dcg_node* descendants[4] = {NULL, NULL, NULL, NULL};
    DCG_CHECK_INT(c_dcg_node_collect_descendants(root, descendants, 4), 3);
    DCG_CHECK(descendants[0] == branch); /* pre-order */
    DCG_CHECK(descendants[1] == yes);
    DCG_CHECK(descendants[2] == no);

    /* The path runs root -> node, the node last. */
    const dcg_node* path[4] = {NULL, NULL, NULL, NULL};
    DCG_CHECK_INT(c_dcg_node_path_to(yes, path, 4), 3);
    DCG_CHECK(path[0] == root);
    DCG_CHECK(path[1] == branch);
    DCG_CHECK(path[2] == yes);

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

static void test_an_arm_is_found_across_condition_copies(void) {
    /* The binding layer builds the graph in one translation unit and hands out
     * the conditions from another (c_edge.pyx vs c_node.pyx), so the condition
     * that built an arm is never the object a lookup holds. The edges have to
     * agree anyway - this is the path the root's first append runs through. */
    dcg_node* parent = dcg_t_node_plain("branch");
    dcg_node* yes    = dcg_t_node_double("yes", 1.0);
    dcg_node* no     = dcg_t_node_double("no", 0.0);
    dcg_node* again  = dcg_t_node_double("again", 2.0);

    dcg_node_edge_condition copied_true  = {.type = DCG_NODE_EDGE_TRUE};
    dcg_node_edge_condition copied_false = {.type = DCG_NODE_EDGE_FALSE};

    DCG_CHECK_INT(c_dcg_node_append(parent, yes, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(parent, no, DCG_FALSE_CONDITION), DCG_OK);

    /* Both arms are found by the copies, and the copies do not cross over. */
    DCG_CHECK(c_dcg_node_child_by_condition(parent, &copied_true) == yes);
    DCG_CHECK(c_dcg_node_child_by_condition(parent, &copied_false) == no);

    /* The copy names an arm that is already taken, and the parent says so. */
    DCG_CHECK_INT(c_dcg_node_append(parent, again, &copied_true), DCG_ERR_DUPLICATE);
    /* With both arms taken there is nothing left for AUTO to infer. */
    DCG_CHECK_INT(c_dcg_node_append_auto(parent, again), DCG_ERR_UNRESOLVED);
    DCG_CHECK(again->parent == NULL);

    c_dcg_node_free(again);
    c_dcg_node_free(yes);
    c_dcg_node_free(no);
    c_dcg_node_free(parent);
}

static void test_find_by_uid(void) {
    dcg_node* root   = build_sample_tree(NULL, NULL, NULL);
    dcg_node* target = c_dcg_node_child_at(c_dcg_node_child_at(root, 0), 1);

    /* Overwrite the minted identity with a known one; the rest keep theirs. */
    static const unsigned char probe[16] = {0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const unsigned char other[16] = {0xbe, 0xef};
    memcpy(target->uid, probe, sizeof(probe));

    DCG_CHECK(c_dcg_node_find_by_uid(root, probe) == target);
    DCG_CHECK(c_dcg_node_find_by_uid(root, other) == NULL);
    DCG_CHECK(c_dcg_node_find_by_uid(NULL, probe) == NULL);

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

static void test_labels(void) {
    dcg_node* node = dcg_t_node_double("d", 1.0);

    /* A label is copied on the way in, so the text can be a local. */
    char mutable_label[] = "first";

    DCG_CHECK_INT(c_dcg_node_label_count(node), 0);
    DCG_CHECK_INT(c_dcg_node_add_label(node, mutable_label), DCG_OK);
    mutable_label[0] = 'X';
    DCG_CHECK(c_dcg_node_has_label(node, "first")); /* the copy does not follow the source */
    DCG_CHECK(!c_dcg_node_has_label(node, "Xirst"));

    DCG_CHECK_INT(c_dcg_node_add_label(node, "first"), DCG_ERR_TYPE); /* duplicates rejected */
    DCG_CHECK_INT(c_dcg_node_label_count(node), 1);
    DCG_CHECK(!c_dcg_node_has_label(node, "missing"));

    DCG_CHECK_INT(c_dcg_node_add_label(node, "second"), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_add_label(node, "second"), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_label_count(node), 2);
    DCG_CHECK(c_dcg_node_has_label(node, "second"));

    DCG_CHECK_INT(c_dcg_node_remove_label(node, "missing"), DCG_ERR_NOT_FOUND);
    DCG_CHECK_INT(c_dcg_node_remove_label(node, "first"), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_label_count(node), 1);
    DCG_CHECK_INT(c_dcg_node_remove_label(node, "second"), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_label_count(node), 0);

    /* NULL-safety across the label API. */
    DCG_CHECK(!c_dcg_node_has_label(NULL, "x"));
    DCG_CHECK_INT(c_dcg_node_add_label(NULL, "x"), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_add_label(node, NULL), DCG_ERR_INVALID_ARG);

    c_dcg_node_free(node);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static void test_validate_accepts_a_well_formed_graph(void) {
    dcg_node*           root = build_sample_tree(NULL, NULL, NULL);
    dcg_validate_report report;

    DCG_CHECK(c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_OK);
    DCG_CHECK_INT(report.errors, 0);
    DCG_CHECK_INT(report.nodes, 4);
    DCG_CHECK_INT(report.depth, 2);
    DCG_CHECK_STR(c_dcg_ret_code_name(report.code), "OK");

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */

    /* A binary branch paired with an else fallback is the canonical two-way
     * decision: it must validate, and the else must count as a branch of its
     * own rather than as an unconditioned one. */
    root               = dcg_t_node_root("Entry Point");
    dcg_node* decide   = dcg_t_node_binary(DCG_OP_LT, "x < 10");
    dcg_node* yes      = dcg_t_node_action(DCG_NODE_LONGACTION, "go long");
    dcg_node* fallback = dcg_t_node_action(DCG_NODE_SHORTACTION, "go short");
    (void) c_dcg_node_append(root, decide, DCG_NO_CONDITION);
    (void) c_dcg_node_append(decide, yes, DCG_TRUE_CONDITION);
    (void) c_dcg_node_append(decide, fallback, DCG_ELSE_CONDITION);

    DCG_CHECK(c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.errors, 0);

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, decide, yes, fallback */

    /* Value branches may repeat as a switch: only mixing them with binary
     * branches is a conflict. */
    root                           = dcg_t_node_root("Entry Point");
    dcg_node* switch_node          = dcg_t_node_call("switch");
    /* A condition linked to a node is owned by it, so it comes from the
     * allocating constructor - the node frees it, the caller does not. */
    dcg_node_edge_condition* key_a = c_dcg_edge_new(dcg_t_var_int(1), "1", NULL);
    dcg_node_edge_condition* key_b = c_dcg_edge_new(dcg_t_var_int(2), "2", NULL);
    (void) c_dcg_node_append(root, switch_node, DCG_NO_CONDITION);
    (void) c_dcg_node_append(switch_node, dcg_t_node_double("one", 1.0), key_a);
    (void) c_dcg_node_append(switch_node, dcg_t_node_double("two", 2.0), key_b);

    DCG_CHECK(c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.errors, 0);

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, switch, one, two */
}

static void test_validate_rejects_broken_graphs(void) {
    dcg_validate_report report;

    /* A detached decision node is not a valid graph root. */
    dcg_node* orphan = dcg_t_node_binary(DCG_OP_ADD, "a + b");
    dcg_t_trace_node("orphan binary", orphan);
    DCG_CHECK(!c_dcg_node_validate(orphan, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_TYPE);
    c_dcg_node_free_generic(orphan); /* the same, for a node no graph holds */

    /* A root with two children. */
    dcg_node* root   = dcg_t_node_root("Entry Point");
    dcg_node* first  = dcg_t_node_double("first", 1.0);
    dcg_node* second = dcg_t_node_double("second", 2.0);
    force_link(root, first, DCG_NO_CONDITION);
    force_link(root, second, DCG_NO_CONDITION);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3); /* root + the two children */

    /* Operator arity: a binary operator with a single operand. */
    root              = dcg_t_node_root("Entry Point");
    dcg_node* binary  = dcg_t_node_binary(DCG_OP_ADD, "a + b");
    dcg_node* operand = dcg_t_node_double("operand", 1.0);
    (void) c_dcg_node_append(root, binary, DCG_NO_CONDITION);
    (void) c_dcg_node_append(binary, operand, DCG_TRUE_CONDITION);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3); /* root, binary, operand */

    /* An action node is a leaf by definition. */
    root                   = dcg_t_node_root("Entry Point");
    dcg_node* action       = dcg_t_node_action(DCG_NODE_LONGACTION, "go long");
    dcg_node* under_action = dcg_t_node_double("under", 1.0);
    (void) c_dcg_node_append(root, action, DCG_NO_CONDITION);
    force_link(action, under_action, DCG_NO_CONDITION);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3); /* root, action, under_action */

    /* An unresolved AUTO edge must never reach a baked graph. */
    root                 = dcg_t_node_root("Entry Point");
    dcg_node* unresolved = dcg_t_node_plain("list");
    dcg_node* auto_child = dcg_t_node_double("auto", 1.0);
    (void) c_dcg_node_append(root, unresolved, DCG_NO_CONDITION);
    force_link(unresolved, auto_child, DCG_AUTO_CONDITION);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_UNRESOLVED);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3); /* root, list, auto child */

    /* Two else branches: the first one is reported as mis-ordered (it is
     * followed by another child) and the second as the duplicate - two
     * defects, both counted. */
    root               = dcg_t_node_root("Entry Point");
    dcg_node* branch   = dcg_t_node_plain("list");
    dcg_node* else_one = dcg_t_node_double("else one", 1.0);
    dcg_node* else_two = dcg_t_node_double("else two", 2.0);
    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    force_link(branch, else_one, DCG_ELSE_CONDITION);
    force_link(branch, else_two, DCG_ELSE_CONDITION);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_EDGE);
    DCG_CHECK_INT(report.errors, 2);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, list, both else branches */

    root                 = dcg_t_node_root("Entry Point");
    branch               = dcg_t_node_plain("list");
    dcg_node* fallback   = dcg_t_node_double("fallback", 0.0);
    dcg_node* after_else = dcg_t_node_double("after", 1.0);
    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    force_link(branch, fallback, DCG_ELSE_CONDITION);
    force_link(branch, after_else, DCG_TRUE_CONDITION);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, list, fallback, after */

    /* Binary branches mixed with value branches. The branch-shape rules
     * apply to decision nodes; a collection stores items keyed by value, so
     * a variadic operator node is the case to build here. */
    root                                 = dcg_t_node_root("Entry Point");
    branch                               = dcg_t_node_call("call");
    dcg_node*               true_branch  = dcg_t_node_double("true", 1.0);
    dcg_node*               value_branch = dcg_t_node_double("value", 2.0);
    dcg_node_edge_condition key;
    c_dcg_condition_init(&key, dcg_t_var_string("k"), "k");
    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    force_link(branch, true_branch, DCG_TRUE_CONDITION);
    force_link(branch, value_branch, &key);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, call, true, value */

    /* An unconditioned branch among several. */
    root                          = dcg_t_node_root("Entry Point");
    branch                        = dcg_t_node_call("call");
    dcg_node*               loose = dcg_t_node_double("loose", 1.0);
    dcg_node*               keyed = dcg_t_node_double("keyed", 2.0);
    dcg_node_edge_condition other_key;
    c_dcg_condition_init(&other_key, dcg_t_var_string("k2"), "k2");
    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    force_link(branch, loose, DCG_NO_CONDITION);
    force_link(branch, keyed, &other_key);
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, call, loose, keyed */

    /* Value-keyed branches are a branch set of their own: two children keyed by
     * value are legitimate, and the rule that rejects a MIX does not touch them. */
    root   = dcg_t_node_root("Entry Point");
    branch = dcg_t_node_plain("keyed");
    dcg_node_edge_condition key_a;
    dcg_node_edge_condition key_b;
    c_dcg_condition_init(&key_a, dcg_t_var_string("a"), "a");
    c_dcg_condition_init(&key_b, dcg_t_var_string("b"), "b");
    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    force_link(branch, dcg_t_node_double("item a", 1.0), &key_a);
    force_link(branch, dcg_t_node_double("item b", 2.0), &key_b);
    DCG_CHECK(c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, item a, item b */

    /* A hand-wired cycle. */
    root           = dcg_t_node_root("Entry Point");
    dcg_node* loop = dcg_t_node_plain("loop");
    dcg_node* tail = dcg_t_node_plain("tail");
    (void) c_dcg_node_append(root, loop, DCG_NO_CONDITION);
    (void) c_dcg_node_append(loop, tail, DCG_TRUE_CONDITION);
    force_link(tail, loop, DCG_FALSE_CONDITION); /* tail -> loop closes the cycle */
    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK(report.errors > 0);

    /* A cycle is a hand-wired state the API never produces, so unwinding it
     * means undoing exactly what the wiring broke: the back edge, the loop's
     * parent pointer, and the root's stale child link. */
    tail->children = NULL;
    loop->parent   = NULL;
    root->children = NULL;
    DCG_CHECK_INT(c_dcg_node_teardown_root(loop), 2); /* loop + tail */
    c_dcg_node_free(root);
}

static void test_validate_null_and_print(void) {
    dcg_validate_report report;
    DCG_CHECK(!c_dcg_node_validate(NULL, &report));
    DCG_CHECK_INT(c_dcg_node_validate_print(NULL, stdout), DCG_ERR_INVALID_ARG);

    dcg_node* root = build_sample_tree(NULL, NULL, NULL);
    DCG_CHECK_INT(c_dcg_node_validate_print(root, stdout), 0);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

/* ------------------------------------------------------------------ */
/* Placeholder consolidation                                           */
/* ------------------------------------------------------------------ */

static void test_consolidate_placeholder(void) {
    /* The normal case: a reserved slot that nothing ever filled. */
    dcg_node* root   = dcg_t_node_root("Entry Point");
    dcg_node* branch = dcg_t_node_binary(DCG_OP_LT, "x < 10");
    dcg_node* filled = dcg_t_node_action(DCG_NODE_LONGACTION, "go long");
    dcg_node* holder = dcg_t_node_placeholder("holder");

    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    (void) c_dcg_node_append(branch, filled, DCG_TRUE_CONDITION);
    (void) c_dcg_node_append(branch, holder, DCG_FALSE_CONDITION);

    DCG_CHECK_INT(c_dcg_node_consolidate_placeholder(root), 1);

    /* The conversion happens in place: same node, same slot, same edge. */
    dcg_node* replacement = c_dcg_node_child_by_condition(branch, DCG_FALSE_CONDITION);
    DCG_CHECK(replacement == holder);
    DCG_CHECK_INT(replacement->ntype, DCG_NODE_NOACTION);
    DCG_CHECK_INT(replacement->autogen, true);
    DCG_CHECK(c_dcg_condition_is_false(replacement->condition_to_parent));

    /* Idempotent, and the result is a clean graph. */
    DCG_CHECK_INT(c_dcg_node_consolidate_placeholder(root), 0);
    DCG_CHECK_INT(c_dcg_node_consolidate_placeholder(NULL), 0);
    DCG_CHECK(c_dcg_node_validate(root, NULL));

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, filled, holder */

    /* A placeholder the builder left holding a subtree keeps it - and the
     * validator reports that mistake rather than the conversion hiding it. */
    root                       = dcg_t_node_root("Entry Point");
    branch                     = dcg_t_node_binary(DCG_OP_LT, "x < 10");
    holder                     = dcg_t_node_placeholder("holder");
    dcg_node*           nested = dcg_t_node_placeholder("nested");
    dcg_node*           other  = dcg_t_node_action(DCG_NODE_NOACTION, "other");
    dcg_validate_report report;

    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    (void) c_dcg_node_append(branch, holder, DCG_TRUE_CONDITION);
    (void) c_dcg_node_append(branch, other, DCG_FALSE_CONDITION);
    (void) c_dcg_node_append(holder, nested, DCG_NO_CONDITION);

    DCG_CHECK_INT(c_dcg_node_consolidate_placeholder(root), 2);
    DCG_CHECK_INT(holder->ntype, DCG_NODE_NOACTION);
    DCG_CHECK_INT(nested->ntype, DCG_NODE_NOACTION);
    DCG_CHECK(c_dcg_node_first_child(holder) == nested); /* the subtree survived */

    DCG_CHECK(!c_dcg_node_validate(root, &report));
    DCG_CHECK_INT(report.code, DCG_ERR_TYPE);

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 5); /* root, branch, holder, nested, other */
}

/* ------------------------------------------------------------------ */
/* Teardown - the one way a graph is freed                             */
/* ------------------------------------------------------------------ */

static void test_teardown_refuses_a_sub_root(void) {
    /* Tearing down from a node that still has a parent would free what the
     * graph above still points at, which is exactly what a sub-root kept for
     * regional management is. It is refused, and nothing is touched. */
    dcg_node* root   = dcg_t_node_root("Entry Point");
    dcg_node* branch = dcg_t_node_plain("branch");
    dcg_node* leaf   = dcg_t_node_double("leaf", 1.0);

    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    (void) c_dcg_node_append(branch, leaf, DCG_TRUE_CONDITION);

    DCG_CHECK_INT(c_dcg_node_teardown_root(branch), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_teardown_root(leaf), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_teardown_root(NULL), DCG_ERR_INVALID_ARG);

    /* The refusal left the graph whole. */
    DCG_CHECK_INT(c_dcg_node_subtree_size(root), 3);
    DCG_CHECK_INT(c_dcg_node_child_count(branch), 1);
    DCG_CHECK(c_dcg_node_first_child(branch) == leaf);
    DCG_CHECK(c_dcg_node_validate(root, NULL));

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3);
}

static void test_teardown_a_shared_node_is_freed_once(void) {
    /* The state the capi allows and this layer does not forbid: a node two
     * parents reach, with only one of the two logged in its metadata - what a
     * jump, or a region hoisted out of its branch, produces. A walk that
     * trusted the tree shape would free it twice; the record is what makes it
     * once, and the sanitizer is what proves it. */
    dcg_node* top    = dcg_t_node_root("Entry Point");
    dcg_node* left   = dcg_t_node_plain("left");
    dcg_node* right  = dcg_t_node_plain("right");
    dcg_node* shared = dcg_t_node_double("shared", 1.0);

    (void) c_dcg_node_append(top, left, DCG_NO_CONDITION);
    (void) c_dcg_node_append(left, right, DCG_TRUE_CONDITION);

    /* Hand-wired: `shared` is the node after `right` in left's list AND the
     * child of `right` - two edges to one block, one logged parent. */
    right->children      = shared;
    shared->parent       = right;
    right->next_sibling  = shared;
    shared->prev_sibling = right;
    shared->next_sibling = NULL;

    /* The tree queries count it twice - which is the point of the record. */
    DCG_CHECK_INT(c_dcg_node_subtree_size(top), 5);
    DCG_CHECK_INT(c_dcg_node_child_count(left), 2);

    dcg_t_trace_tree("shared child (left -> {right, shared}, right -> shared)", top);
    DCG_CHECK_INT(c_dcg_node_teardown_root(top), 4); /* four blocks, freed once each */
}

static void test_teardown_of_a_cycle_terminates(void) {
    /* A back edge - the jump a future build may wire - must not make the
     * teardown spin, and must not let a free walk into a node that is already
     * gone: the record ends the walk, and the edges are dropped before the
     * first free. */
    dcg_node* top  = dcg_t_node_root("Entry Point");
    dcg_node* loop = dcg_t_node_plain("loop");
    dcg_node* tail = dcg_t_node_plain("tail");

    (void) c_dcg_node_append(top, loop, DCG_NO_CONDITION);
    (void) c_dcg_node_append(loop, tail, DCG_TRUE_CONDITION);
    force_link(tail, loop, DCG_FALSE_CONDITION); /* tail -> loop closes the cycle */

    /* The renderer and the tree queries assume an acyclic graph, so the shape
     * is stated rather than drawn - the teardown is the walk that does not. */
    (void) printf("    %-26s loop -> tail -> loop (3 nodes, one back edge)\n", "cycle");
    DCG_CHECK_INT(c_dcg_node_teardown_root(top), 3); /* each node exactly once */
}

/* ------------------------------------------------------------------ */
/* Closing a branch                                                    */
/* ------------------------------------------------------------------ */

/* The arm a fill added, whatever it was filled with. */
static void fill_checks(dcg_node* node, const dcg_node_edge_condition* condition) {
    dcg_node* filled = c_dcg_node_child_by_condition(node, condition);
    DCG_CHECK(filled != NULL);
    if (!filled) return;
    DCG_CHECK_INT(filled->ntype, DCG_NODE_NOACTION);
    DCG_CHECK(filled->autogen); /* auto-generated: a later consolidation can tell */
    DCG_CHECK_STR(filled->repr, DCG_DEF_REPR_NOACTION);
}

static void test_auto_fill_no_branch(void) {
    /* A node that never got a branch is closed with an unconditioned no-action. */
    dcg_node* node = dcg_t_node_plain("empty");

    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 1);
    fill_checks(node, DCG_NO_CONDITION);

    /* Idempotent: the second call sees a closed node and adds nothing. */
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 1);

    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 2);
    DCG_CHECK_INT(c_dcg_node_auto_fill(NULL), DCG_ERR_INVALID_ARG);
}

static void test_auto_fill_one_branch(void) {
    /* A lone binary arm gains its opposite: the node becomes total. */
    dcg_node* node = dcg_t_node_plain("binary");
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("then", 1.0), DCG_TRUE_CONDITION), DCG_OK);

    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 2);
    fill_checks(node, DCG_FALSE_CONDITION);

    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3);

    node = dcg_t_node_plain("binary");
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("else", 2.0), DCG_FALSE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    fill_checks(node, DCG_TRUE_CONDITION);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3);

    /* An unconditioned arm already says all the node can: nothing is added. */
    node = dcg_t_node_plain("unconditioned");
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("only", 1.0), DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 1);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 2);

    /* A value-keyed arm stands alone too: a node keyed by value is complete. */
    node = dcg_t_node_plain("keyed");
    dcg_node_edge_condition key;
    c_dcg_condition_init(&key, dcg_t_var_string("a"), "a");
    force_link(node, dcg_t_node_double("item", 1.0), &key);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 1);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 2);
}

static void test_auto_fill_else_only(void) {
    /* An ELSE-only node is a node waiting for its TRUE arm, and the arm goes in
     * FRONT of the fallback - the else-last rule is not negotiable. */
    dcg_node* root = dcg_t_node_root("Entry Point");
    dcg_node* node = dcg_t_node_plain("else-only");
    DCG_CHECK_INT(c_dcg_node_append(root, node, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("fallback", 1.0), DCG_ELSE_CONDITION), DCG_OK);

    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 2);
    fill_checks(node, DCG_TRUE_CONDITION);
    DCG_CHECK(c_dcg_node_first_child(node) == c_dcg_node_child_by_condition(node, DCG_TRUE_CONDITION));
    DCG_CHECK(c_dcg_node_last_child(node) == c_dcg_node_child_by_condition(node, DCG_ELSE_CONDITION)); /* still last */
    DCG_CHECK(c_dcg_node_validate(root, NULL)); /* a closed node is a valid graph */

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4);
}

static void test_auto_fill_two_branches(void) {
    dcg_node*             node = NULL;
    dcg_node_edge_condition key_a;
    dcg_node_edge_condition key_b;

    /* Two binaries stand as they are. */
    node = dcg_t_node_plain("binary pair");
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("then", 1.0), DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("else", 2.0), DCG_FALSE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 2);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3);

    /* One arm plus a fallback stands too. */
    node = dcg_t_node_plain("value + else");
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("then", 1.0), DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("fallback", 2.0), DCG_ELSE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 2);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3);

    /* Two value-keyed arms gain the protective fallback that makes them total. */
    node = dcg_t_node_plain("keyed pair");
    c_dcg_condition_init(&key_a, dcg_t_var_string("a"), "a");
    c_dcg_condition_init(&key_b, dcg_t_var_string("b"), "b");
    force_link(node, dcg_t_node_double("item a", 1.0), &key_a);
    force_link(node, dcg_t_node_double("item b", 2.0), &key_b);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 3);
    fill_checks(node, DCG_ELSE_CONDITION);
    DCG_CHECK(c_dcg_node_last_child(node) == c_dcg_node_child_by_condition(node, DCG_ELSE_CONDITION));
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 4);

    /* An unconditioned arm cannot share a node - that is a build error. */
    node = dcg_t_node_plain("unconditioned pair");
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("only", 1.0), DCG_NO_CONDITION), DCG_OK);
    force_link(node, dcg_t_node_double("extra", 2.0), DCG_TRUE_CONDITION);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3);

    /* A binary beside a value-keyed arm is contradictory. */
    node = dcg_t_node_plain("mixed pair");
    c_dcg_condition_init(&key_a, dcg_t_var_string("a"), "a");
    force_link(node, dcg_t_node_double("item", 1.0), &key_a);
    force_link(node, dcg_t_node_double("then", 2.0), DCG_TRUE_CONDITION);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3);
}

static void test_auto_fill_many_branches(void) {
    dcg_node*             node = NULL;
    dcg_node_edge_condition key_a;
    dcg_node_edge_condition key_b;
    dcg_node_edge_condition key_c;

    /* Value-keyed arms beyond a pair gain the fallback too. */
    node = dcg_t_node_plain("keyed trio");
    c_dcg_condition_init(&key_a, dcg_t_var_string("a"), "a");
    c_dcg_condition_init(&key_b, dcg_t_var_string("b"), "b");
    c_dcg_condition_init(&key_c, dcg_t_var_string("c"), "c");
    force_link(node, dcg_t_node_double("item a", 1.0), &key_a);
    force_link(node, dcg_t_node_double("item b", 2.0), &key_b);
    force_link(node, dcg_t_node_double("item c", 3.0), &key_c);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 4);
    fill_checks(node, DCG_ELSE_CONDITION);

    /* One that already ends with a fallback is left alone. */
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 4);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 5);

    /* A binary among three is one branch too many. */
    node = dcg_t_node_plain("keyed pair + binary");
    c_dcg_condition_init(&key_a, dcg_t_var_string("a"), "a");
    c_dcg_condition_init(&key_b, dcg_t_var_string("b"), "b");
    force_link(node, dcg_t_node_double("item a", 1.0), &key_a);
    force_link(node, dcg_t_node_double("item b", 2.0), &key_b);
    force_link(node, dcg_t_node_double("then", 3.0), DCG_TRUE_CONDITION);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 4);

    /* So is an unconditioned arm. */
    node = dcg_t_node_plain("keyed pair + unconditioned");
    c_dcg_condition_init(&key_a, dcg_t_var_string("a"), "a");
    c_dcg_condition_init(&key_b, dcg_t_var_string("b"), "b");
    force_link(node, dcg_t_node_double("item a", 1.0), &key_a);
    force_link(node, dcg_t_node_double("item b", 2.0), &key_b);
    force_link(node, dcg_t_node_double("only", 3.0), DCG_NO_CONDITION);
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_ERR_EDGE);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 4);
}

static void test_auto_fill_closes_a_built_branch(void) {
    /* The shape a `with` block produces, in the order it produces it: a node is
     * entered with both arms RESERVED, a nested build fills one, and the close
     * has nothing left to add - the arm that was never built is still holding
     * its placeholder, and consolidation is what turns it into a no-action. */
    dcg_node* root = dcg_t_node_root("Entry Point");
    dcg_node* node = dcg_t_node_plain("decision");
    DCG_CHECK_INT(c_dcg_node_append(root, node, DCG_NO_CONDITION), DCG_OK);

    dcg_node* reserved_true  = c_dcg_node_new_placeholder(NULL);
    dcg_node* reserved_false = c_dcg_node_new_placeholder(NULL);
    DCG_CHECK_INT(c_dcg_node_append(node, reserved_true, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(node, reserved_false, DCG_FALSE_CONDITION), DCG_OK);

    /* One arm is built out; the other still holds its placeholder. */
    DCG_CHECK_INT(c_dcg_node_replace(reserved_true, dcg_t_node_double("built", 1.0)), DCG_OK);
    c_dcg_node_free(reserved_true);

    /* Both arms are binaries, so the close has nothing to say. */
    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 2);

    DCG_CHECK_INT(c_dcg_node_consolidate_placeholder(node), 1);
    DCG_CHECK_INT(reserved_false->ntype, DCG_NODE_NOACTION);
    DCG_CHECK(reserved_false->autogen);
    DCG_CHECK_STR(reserved_false->repr, DCG_DEF_REPR_NOACTION); /* the arm reads as a no-action, as it does in capi */

    DCG_CHECK(c_dcg_node_validate(root, NULL));
    dcg_t_trace_tree("closed branch (built + consolidated)", root);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4);
}

static void test_an_entered_binary_reserves_true_first(void) {
    /* The two arms a branching node reserves go in the order the evaluator reads
     * them: TRUE first, FALSE second. The capi keeps the pair the other way
     * round, and this layer deliberately does not - the child order here IS the
     * reading order, so a render shows the true branch above the fallback and a
     * sequential check reaches it first. */
    dcg_node* node = dcg_t_node_plain("decision");
    DCG_CHECK_INT(c_dcg_node_ctx_enter_binary(node, NULL), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 2);

    dcg_node* first = c_dcg_node_first_child(node);
    dcg_node* last  = c_dcg_node_last_child(node);
    DCG_CHECK_INT(first->ntype, DCG_NODE_PLACEHOLDER);
    DCG_CHECK_INT(last->ntype, DCG_NODE_PLACEHOLDER);
    DCG_CHECK(c_dcg_condition_is_true(first->condition_to_parent));
    DCG_CHECK(c_dcg_condition_is_false(last->condition_to_parent));

    /* The fill takes the first reservation, so the first branch built lands on
     * the TRUE arm - and filling it leaves the order as it was. */
    DCG_CHECK(c_dcg_node_get_placeholder(node) == first);
    DCG_CHECK_INT(c_dcg_node_replace(first, dcg_t_node_double("built", 1.0)), DCG_OK);
    c_dcg_node_free(first);
    DCG_CHECK(c_dcg_condition_is_true(c_dcg_node_first_child(node)->condition_to_parent));
    DCG_CHECK(c_dcg_condition_is_false(c_dcg_node_last_child(node)->condition_to_parent));
    DCG_CHECK(c_dcg_node_get_placeholder(node) == last);

    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3); /* node, built, the FALSE arm */
}

static void test_get_placeholder_takes_the_first_standing(void) {
    /* A node with both arms reserved hands back the FIRST standing placeholder,
     * which is the TRUE arm of the pair an entering reserves. Anything else
     * would fill the fallback first and leave the pair in the order the read
     * does not use. */
    dcg_node* node = dcg_t_node_plain("decision");

    dcg_node* reserved_true  = c_dcg_node_new_placeholder(NULL);
    dcg_node* reserved_false = c_dcg_node_new_placeholder(NULL);
    DCG_CHECK_INT(c_dcg_node_append(node, reserved_true, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(node, reserved_false, DCG_FALSE_CONDITION), DCG_OK);

    DCG_CHECK(c_dcg_node_get_placeholder(node) == reserved_true);

    /* Fill it, and the one that is left is the FALSE arm's. */
    DCG_CHECK_INT(c_dcg_node_replace(reserved_true, dcg_t_node_double("built", 1.0)), DCG_OK);
    c_dcg_node_free(reserved_true);
    DCG_CHECK(c_dcg_node_get_placeholder(node) == reserved_false);

    /* A node with no placeholder at all gets one on the inferred edge. */
    dcg_node* fresh = dcg_t_node_plain("fresh");
    DCG_CHECK(c_dcg_node_get_placeholder(fresh) != NULL);
    DCG_CHECK_INT(c_dcg_node_child_count(fresh), 1);
    DCG_CHECK(c_dcg_condition_is_true(c_dcg_node_first_child(fresh)->condition_to_parent));

    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 3); /* node, built, reserved_false */
    DCG_CHECK_INT(c_dcg_node_teardown_root(fresh), 2);
    DCG_CHECK(c_dcg_node_get_placeholder(NULL) == NULL);
}

static void test_auto_fill_under_a_root(void) {
    /* A node is only judgeable once it hangs in a graph: a parentless node that
     * is not a root is a detached fragment, and validation says so. */
    dcg_node* root = dcg_t_node_root("Entry Point");
    dcg_node* node = dcg_t_node_plain("decision");
    DCG_CHECK_INT(c_dcg_node_append(root, node, DCG_NO_CONDITION), DCG_OK);

    DCG_CHECK_INT(c_dcg_node_auto_fill(node), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(node), 1);
    DCG_CHECK(c_dcg_node_validate(root, NULL));

    /* The same node on its own is not a graph. */
    dcg_node* loose = dcg_t_node_plain("loose");
    DCG_CHECK_INT(c_dcg_node_auto_fill(loose), DCG_OK);
    DCG_CHECK(!c_dcg_node_validate(loose, NULL));

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3);
    DCG_CHECK_INT(c_dcg_node_teardown_root(loose), 2);
}

int main(void) {
    (void) printf("test_c_node_hierarchy\n");
    DCG_RUN(test_append_and_order);
    DCG_RUN(test_append_rejections);
    DCG_RUN(test_else_rules);
    DCG_RUN(test_root_rules);
    DCG_RUN(test_inference);
    DCG_RUN(test_autogen_edge_reuse);
    DCG_RUN(test_insert_at);
    DCG_RUN(test_detach_replace_remove);
    DCG_RUN(test_clear_children);
    DCG_RUN(test_frozen_parent);
    DCG_RUN(test_traversal_queries);
    DCG_RUN(test_an_arm_is_found_across_condition_copies);
    DCG_RUN(test_find_by_uid);
    DCG_RUN(test_labels);
    DCG_RUN(test_validate_accepts_a_well_formed_graph);
    DCG_RUN(test_validate_rejects_broken_graphs);
    DCG_RUN(test_validate_null_and_print);
    DCG_RUN(test_consolidate_placeholder);
    DCG_RUN(test_auto_fill_no_branch);
    DCG_RUN(test_auto_fill_one_branch);
    DCG_RUN(test_auto_fill_else_only);
    DCG_RUN(test_auto_fill_two_branches);
    DCG_RUN(test_auto_fill_many_branches);
    DCG_RUN(test_auto_fill_closes_a_built_branch);
    DCG_RUN(test_get_placeholder_takes_the_first_standing);
    DCG_RUN(test_an_entered_binary_reserves_true_first);
    DCG_RUN(test_auto_fill_under_a_root);
    DCG_RUN(test_teardown_refuses_a_sub_root);
    DCG_RUN(test_teardown_a_shared_node_is_freed_once);
    DCG_RUN(test_teardown_of_a_cycle_terminates);
    DCG_SUMMARY("test_c_node_hierarchy");
    return dcg_test_failures == 0 ? 0 : 1;
}
