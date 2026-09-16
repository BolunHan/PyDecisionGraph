/*
 * c_node.h - the callback protocol: the per-node eval hook triple hosted by
 * dcg_node_eval_ctx, and the fire-and-forget mutation observers.
 */

#include <decision_graph/decision_tree/bake/c_node.h>

#include <decision_graph/decision_tree/bake/c_collection.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_expr.h>

#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

/* ------------------------------------------------------------------ */
/* Eval hooks                                                          */
/* ------------------------------------------------------------------ */

/*
 * The hooks are plain fields of the node's evaluation context, which is a
 * plain struct member of the node - there are no accessor wrappers to test.
 * These cases exercise the fields the way the evaluator will: reach the
 * context through the node, call the three hooks in order, and read the value
 * the eval hook left in node->out.
 */

typedef struct hook_log {
    int       pre_calls;
    int       eval_calls;
    int       post_calls;
    int       order[3]; /* records which hook ran when */
    int       order_len;
    dcg_node* seen_self[3];
    void*     seen_user_data[3];
} hook_log;

static hook_log g_log;

static void     reset_log(void) {
    (void) memset(&g_log, 0, sizeof(g_log));
}

/*
 * One function per phase: the three hooks of a node share a single user_data,
 * so a hook that served several phases could not tell them apart by that
 * pointer - and does not need to, since the evaluator calls each in turn.
 */
static void log_visit(int slot, dcg_node* node, void* user_data) {
    if (g_log.order_len < 3) g_log.order[g_log.order_len++] = slot;
    g_log.seen_self[slot]      = node;
    g_log.seen_user_data[slot] = user_data;
}

static int pre_hook(dcg_node* node, void* user_data) {
    g_log.pre_calls++;
    log_visit(DCG_HOOK_PRE_EVAL, node, user_data);
    return DCG_OK;
}

static int eval_hook(dcg_node* node, void* user_data) {
    g_log.eval_calls++;
    log_visit(DCG_HOOK_EVAL, node, user_data);
    /* The hook reaches the value slot through its node. */
    (void) c_dcg_var_init_double(&node->out, 42.0);
    return DCG_OK;
}

static int post_hook(dcg_node* node, void* user_data) {
    g_log.post_calls++;
    log_visit(DCG_HOOK_POST_EVAL, node, user_data);
    return DCG_OK;
}

static int failing_hook(dcg_node* node, void* user_data) {
    (void) node;
    (void) user_data;
    return DCG_ERR_TYPE;
}

static void test_hook_context_is_a_struct(void) {
    dcg_node* node = dcg_t_node_double("d", 1.0);

    /* A fresh node has no hooks and no data. */
    DCG_CHECK(node->eval_ctx.pre_eval_fn == NULL);
    DCG_CHECK(node->eval_ctx.eval_fn == NULL);
    DCG_CHECK(node->eval_ctx.post_eval_fn == NULL);
    DCG_CHECK(node->eval_ctx.user_data == NULL);
    DCG_CHECK_INT(node->eval_ctx.visits, 0);

    int marker                  = 7;
    node->eval_ctx.pre_eval_fn  = pre_hook;
    node->eval_ctx.eval_fn      = eval_hook;
    node->eval_ctx.post_eval_fn = post_hook;
    node->eval_ctx.user_data    = &marker;

    DCG_CHECK(node->eval_ctx.eval_fn == eval_hook);
    DCG_CHECK(node->eval_ctx.user_data == &marker);

    /* The context is reachable through the node, which is all a hook gets. */
    dcg_node_eval_ctx* ctx = &node->eval_ctx;
    DCG_CHECK(ctx->pre_eval_fn == pre_hook);
    DCG_CHECK(ctx->post_eval_fn == post_hook);

    /* Dropping one hook leaves the others and the shared data alone. */
    node->eval_ctx.eval_fn = NULL;
    DCG_CHECK(node->eval_ctx.pre_eval_fn == pre_hook);
    DCG_CHECK(node->eval_ctx.user_data == &marker);

    c_dcg_node_free(node);
}

static void test_hook_dispatch(void) {
    reset_log();

    dcg_node* node   = dcg_t_node_double("d", 1.0);
    int       marker = 5;

    node->eval_ctx.pre_eval_fn  = pre_hook;
    node->eval_ctx.eval_fn      = eval_hook;
    node->eval_ctx.post_eval_fn = post_hook;
    node->eval_ctx.user_data    = &marker;

    /* The evaluator's per-node sequence. */
    DCG_CHECK_INT(node->eval_ctx.pre_eval_fn(node, node->eval_ctx.user_data), DCG_OK);
    DCG_CHECK_INT(node->eval_ctx.eval_fn(node, node->eval_ctx.user_data), DCG_OK);
    DCG_CHECK_INT(node->eval_ctx.post_eval_fn(node, node->eval_ctx.user_data), DCG_OK);

    DCG_CHECK_INT(g_log.pre_calls, 1);
    DCG_CHECK_INT(g_log.eval_calls, 1);
    DCG_CHECK_INT(g_log.post_calls, 1);
    DCG_CHECK_INT(g_log.order_len, 3);
    DCG_CHECK_INT(g_log.order[0], DCG_HOOK_PRE_EVAL);
    DCG_CHECK_INT(g_log.order[1], DCG_HOOK_EVAL);
    DCG_CHECK_INT(g_log.order[2], DCG_HOOK_POST_EVAL);

    /* Every hook sees its node and the one data its node carries. */
    for (int slot_index = 0; slot_index < 3; slot_index++) {
        DCG_CHECK(g_log.seen_self[slot_index] == node);
        DCG_CHECK(g_log.seen_user_data[slot_index] == &marker);
    }

    /* The eval hook produced the value, writing into the node's own slot. */
    DCG_CHECK(node->out.value.as_double == 42.0);

    /* The run state the evaluator threads through lives here too. */
    node->eval_ctx.depth = 3;
    node->eval_ctx.run   = (void*) 0xabc;
    node->eval_ctx.flags |= DCG_EVAL_FLAG_TRACE;
    DCG_CHECK_INT(node->eval_ctx.depth, 3);
    DCG_CHECK(node->eval_ctx.run == (void*) 0xabc);

    c_dcg_node_free(node);
}

static void test_hook_visits_and_failures(void) {
    dcg_node* node = dcg_t_node_double("d", 1.0);

    /* A node is evaluated as many times as the evaluator says; the counter is
     * the evaluator's to keep, and a failure is handed straight back. */
    node->eval_ctx.eval_fn = failing_hook;
    DCG_CHECK_INT(node->eval_ctx.eval_fn(node, NULL), DCG_ERR_TYPE);

    node->eval_ctx.eval_fn = eval_hook;
    node->eval_ctx.visits++;
    node->eval_ctx.visits++;
    DCG_CHECK_INT(node->eval_ctx.visits, 2);

    c_dcg_node_free(node);
}

/* ------------------------------------------------------------------ */
/* Mutation callbacks                                                  */
/* ------------------------------------------------------------------ */

typedef struct callback_log {
    int       calls;
    int       last_event;
    dcg_node* last_self;
    dcg_node* last_subject;
    uint64_t  last_seq_id;
    void*     last_user_data;
} callback_log;

static callback_log g_cb_log;

static void         record_callback(dcg_node_event event, dcg_node* self, dcg_node* subject, uint64_t seq_id, void* user_data) {
    g_cb_log.calls++;
    g_cb_log.last_event     = (int) event;
    g_cb_log.last_self      = self;
    g_cb_log.last_subject   = subject;
    g_cb_log.last_seq_id    = seq_id;
    g_cb_log.last_user_data = user_data;
}

static uintptr_t g_self_unregister_id;
static dcg_node* g_self_unregister_node;
static int       g_self_unregister_calls;

static void      self_unregistering_callback(dcg_node_event event, dcg_node* self, dcg_node* subject, uint64_t seq_id, void* user_data) {
    (void) event;
    (void) self;
    (void) subject;
    (void) seq_id;
    (void) user_data;
    g_self_unregister_calls++;
    /* Dropping the registration mid-invocation must be safe. */
    (void) c_dcg_node_unregister_callback(g_self_unregister_node, g_self_unregister_id);
}

static void test_callback_registration(void) {
    dcg_node* node    = dcg_t_node_collection(DCG_NODE_LIST, "list");
    uintptr_t first   = 0;
    uintptr_t second  = 0;
    int       payload = 0;

    DCG_CHECK_INT(c_dcg_node_callback_count(node), 0);
    DCG_CHECK_INT(c_dcg_node_register_callback(node, record_callback, &payload, &first), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_register_callback(node, record_callback, &payload, &second), DCG_OK);
    DCG_CHECK(first != 0 && second != 0 && first != second);
    DCG_CHECK_INT(c_dcg_node_callback_count(node), 2);

    /* Unregistration takes the opaque id, not the function pointer. */
    DCG_CHECK_INT(c_dcg_node_unregister_callback(node, first), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_callback_count(node), 1);
    DCG_CHECK_INT(c_dcg_node_unregister_callback(node, first), DCG_ERR_NOT_FOUND);
    DCG_CHECK_INT(c_dcg_node_unregister_callback(node, 0xdeadbeef), DCG_ERR_NOT_FOUND);

    /* Rejected registrations and NULL-safety. */
    DCG_CHECK_INT(c_dcg_node_register_callback(node, NULL, NULL, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_register_callback(NULL, record_callback, NULL, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_unregister_callback(NULL, 0), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_callback_count(NULL), 0);
    c_dcg_node_invoke_callbacks(NULL, DCG_NODE_EVENT_MODIFIED, NULL, 0);

    c_dcg_node_free(node);
}

static void test_callback_events(void) {
    (void) memset(&g_cb_log, 0, sizeof(g_cb_log));

    dcg_node* parent      = dcg_t_node_collection(DCG_NODE_LIST, "parent");
    dcg_node* child       = dcg_t_node_double("child", 1.0);
    dcg_node* replacement = dcg_t_node_double("replacement", 2.0);
    dcg_node* grandchild  = dcg_t_node_double("grandchild", 3.0);
    int       payload     = 7;

    DCG_CHECK_INT(c_dcg_node_register_callback(parent, record_callback, &payload, NULL), DCG_OK);

    /* Linking a child notifies the parent and hands over the child. */
    DCG_CHECK_INT(c_dcg_node_append(parent, child, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(g_cb_log.calls, 1);
    DCG_CHECK_INT(g_cb_log.last_event, DCG_NODE_EVENT_CHILD_ADDED);
    DCG_CHECK(g_cb_log.last_self == parent);
    DCG_CHECK(g_cb_log.last_subject == child);
    DCG_CHECK(g_cb_log.last_user_data == &payload);

    /* Detaching notifies the former parent. */
    DCG_CHECK_INT(c_dcg_node_detach(child), DCG_OK);
    DCG_CHECK_INT(g_cb_log.last_event, DCG_NODE_EVENT_CHILD_REMOVED);
    DCG_CHECK(g_cb_log.last_subject == child);

    /* Replacing notifies with the surviving node as the subject. */
    DCG_CHECK_INT(c_dcg_node_append(parent, child, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_replace(child, replacement), DCG_OK);
    DCG_CHECK_INT(g_cb_log.last_event, DCG_NODE_EVENT_CHILD_ADDED);
    DCG_CHECK(g_cb_log.last_subject == replacement);

    /* replace() only detaches the displaced node - owning it stays with the
     * caller, so the leak checker would notice if this free were missing. */
    DCG_CHECK(child->parent == NULL);
    c_dcg_node_free(child);

    /* Labels and hooks are metadata changes. */
    DCG_CHECK_INT(c_dcg_node_add_label(parent, "tag"), DCG_OK);
    DCG_CHECK_INT(g_cb_log.last_event, DCG_NODE_EVENT_MODIFIED);

    /* Clearing drops the whole child list and says so once - the grandchild
     * comes down with its parent, since a child drop tears the subtree down. */
    DCG_CHECK_INT(c_dcg_node_append_auto(replacement, grandchild), DCG_OK);
    (void) memset(&g_cb_log, 0, sizeof(g_cb_log));
    DCG_CHECK_INT(c_dcg_node_clear_children(parent), 2); /* the child and its own child */
    DCG_CHECK_INT(g_cb_log.calls, 1);
    DCG_CHECK_INT(g_cb_log.last_event, DCG_NODE_EVENT_CLEARED);

    /* Teardown announces itself before the buf is zeroed. */
    (void) memset(&g_cb_log, 0, sizeof(g_cb_log));
    c_dcg_node_free(parent);
    DCG_CHECK_INT(g_cb_log.calls, 1);
    DCG_CHECK_INT(g_cb_log.last_event, DCG_NODE_EVENT_FREED);
    DCG_CHECK_INT(g_cb_log.last_seq_id, (uint64_t) -1); /* lifecycle has no caller to suppress */
}

static void test_callback_self_unregistration(void) {
    dcg_node* node = dcg_t_node_collection(DCG_NODE_LIST, "list");

    g_self_unregister_node  = node;
    g_self_unregister_calls = 0;
    DCG_CHECK_INT(c_dcg_node_register_callback(node, self_unregistering_callback, NULL, &g_self_unregister_id), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_callback_count(node), 1);

    /* The list snapshot inside invoke must survive the callback removing itself. */
    c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_MODIFIED, node, 1);
    DCG_CHECK_INT(g_self_unregister_calls, 1);
    DCG_CHECK_INT(c_dcg_node_callback_count(node), 0);

    /* Nothing left to call, and no dangling registration. */
    c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_MODIFIED, node, 1);
    DCG_CHECK_INT(g_self_unregister_calls, 1);

    c_dcg_node_free(node);
}

static void test_callback_seq_id(void) {
    /* A caller passes its own seq_id so a callback can recognise - and
     * ignore - the mutation it caused itself. */
    (void) memset(&g_cb_log, 0, sizeof(g_cb_log));

    dcg_node* node  = dcg_t_node_collection(DCG_NODE_LIST, "list");
    uint64_t  my_id = c_dcg_node_gen_seq_id(node);
    uintptr_t id    = 0;

    DCG_CHECK_INT(c_dcg_node_register_callback(node, record_callback, NULL, &id), DCG_OK);
    c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_MODIFIED, node, my_id);
    DCG_CHECK_INT(g_cb_log.last_seq_id, my_id);
    DCG_CHECK(g_cb_log.last_seq_id == my_id);
    DCG_CHECK_INT(c_dcg_node_unregister_callback(node, id), DCG_OK);

    c_dcg_node_free(node);
}

int main(void) {
    (void) printf("test_c_node_callbacks\n");
    DCG_RUN(test_hook_context_is_a_struct);
    DCG_RUN(test_hook_dispatch);
    DCG_RUN(test_hook_visits_and_failures);
    DCG_RUN(test_callback_registration);
    DCG_RUN(test_callback_events);
    DCG_RUN(test_callback_self_unregistration);
    DCG_RUN(test_callback_seq_id);
    DCG_SUMMARY("test_c_node_callbacks");
    return dcg_test_failures == 0 ? 0 : 1;
}
