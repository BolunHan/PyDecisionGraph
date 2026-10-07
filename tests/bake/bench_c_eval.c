/*
 * What the eval dispatch costs: the same graphs, timed both ways.
 *
 * The switch this measures is DCG_EVAL_DIRECT_HOOKS (c_node.h). With it off, a
 * node's value is found by dispatching on its type at every evaluation; with it
 * on, each node was taught its own rule when it was built, and the evaluator
 * calls a pointer. The two builds are the same program otherwise, so the
 * difference is the dispatch and nothing else.
 *
 * What is measured has to be the layer's own work: a graph built in C, walked in
 * C, from a C caller. Timing from Python would measure the binding, not the
 * evaluator.
 *
 * The graphs are built to look like what a decision graph is - a store of
 * entries, branches over reads, arithmetic between them, and action leaves -
 * because a graph of literals alone would measure the arithmetic and skip the
 * reads, which is where the layers' rules actually differ (a read is the one
 * node whose evaluation asks another object what to do).
 *
 *   make bench
 */

#include <time.h>

#include <decision_graph/decision_tree/bake/c_collections.h>
#include <decision_graph/decision_tree/bake/c_eval.h>

#include "test_util.h"

/* The assertion harness is included for its fixtures; its counters belong to the
 * suites, so the benchmark marks them as wanted. */
static void bench_ignore_harness(void) {
    (void) dcg_test_checks;
    (void) dcg_test_failures;
}

// ========== The graphs ==========

/** Graphs built, and the entries each one's store holds. */
#define BENCH_GRAPHS 64
#define BENCH_ENTRIES 4

/** Every graph is this: a store, a root, three branches, four leaves. */
#define BENCH_NODES 8

typedef struct bench_case {
    dcg_mapping_lgroup* store;
    dcg_root_node*      root;
    dcg_variable_node*  reads[BENCH_ENTRIES];
} bench_case;

static dcg_var_t* bench_slot(dcg_mapping_lgroup* store, size_t index) {
    char  key[8];
    int   len = snprintf(key, sizeof(key), "x%zu", index);
    dcg_var_t* slot = NULL;
    if (c_dcg_mapping_lgroup_get_create_slot(store, key, (size_t) len, NULL, &slot) != DCG_OK) return NULL;
    return slot;
}

/**
 * One graph: three branches over four read entries, four action leaves.
 *
 *   root
 *    +-- b0 = (x0 > x1)          [the root's one edge]
 *         +-- b1 = (x1 + x2 > 1) [true]
 *         |    +-- b2 = (x2 > x3)      [true]
 *         |    |    +-- long           [true]
 *         |    |    +-- short          [false]
 *         |    +-- cancel              [false]
 *         +-- clear                    [false]
 *
 * Three levels of branch and three expression nodes, each over reads - which is
 * the shape the dispatch and the direct hook evaluate differently.
 */
static int bench_build(bench_case* bench, size_t index) {
    bench->store = c_dcg_mapping_lgroup_new(NULL, BENCH_ENTRIES, NULL);
    if (!bench->store) return 0;

    for (size_t i = 0; i < BENCH_ENTRIES; i++) {
        dcg_var_t* slot = bench_slot(bench->store, i);
        if (!slot) return 0;
    }
    for (size_t i = 0; i < BENCH_ENTRIES; i++) {
        bench->reads[i] = c_dcg_mapping_lgroup_get_node(bench->store, (char[]) { 'x', (char) ('0' + i), '\0' }, 2, NULL);
        if (!bench->reads[i]) return 0;
    }

    /* Values that differ per graph, so the graphs do not all decide the same way. */
    double offset = (double) (index % 7) * 0.25 - 0.75;
    (void) c_dcg_mapping_lgroup_set_double(bench->store, "x0", 2, 1.0 + offset);
    (void) c_dcg_mapping_lgroup_set_double(bench->store, "x1", 2, 0.5 + offset);
    (void) c_dcg_mapping_lgroup_set_double(bench->store, "x2", 2, 0.75 - offset);
    (void) c_dcg_mapping_lgroup_set_double(bench->store, "x3", 2, 0.25 - offset);

    bench->root = c_dcg_node_new_root(NULL, NULL);
    if (!bench->root) return 0;

    dcg_node* long_  = dcg_t_node_action(DCG_NODE_LONGACTION, "long");
    dcg_node* short_ = dcg_t_node_action(DCG_NODE_SHORTACTION, "short");
    dcg_node* cancel = dcg_t_node_action(DCG_NODE_CANCELACTION, "cancel");
    dcg_node* clear  = dcg_t_node_action(DCG_NODE_CLEARACTION, "clear");

    dcg_node* b2 = NULL;
    dcg_node* b1 = NULL;
    dcg_node* b0 = NULL;
    {
        dcg_constant_node* one = c_dcg_node_new_const_double(1.0, NULL);
        b2 = dcg_t_node_binary(DCG_OP_GT, "x2 > x3");
        b1 = dcg_t_node_binary(DCG_OP_GT, "x1 + x2 > 1");
        b0 = dcg_t_node_binary(DCG_OP_GT, "x0 > x1");
        c_ap_decref(&one->base);
    }
    if (!b0 || !b1 || !b2) return 0;

    /* The branches read the store: the operands are the reads, bound in order. */
    if (c_dcg_node_expr_bind((dcg_expression_node*) b0, 0, &bench->reads[0]->base) != DCG_OK) return 0;
    if (c_dcg_node_expr_bind((dcg_expression_node*) b0, 1, &bench->reads[1]->base) != DCG_OK) return 0;
    if (c_dcg_node_expr_bind((dcg_expression_node*) b1, 0, &bench->reads[2]->base) != DCG_OK) return 0;
    if (c_dcg_node_expr_bind((dcg_expression_node*) b1, 1, &bench->reads[1]->base) != DCG_OK) return 0;
    if (c_dcg_node_expr_bind((dcg_expression_node*) b2, 0, &bench->reads[2]->base) != DCG_OK) return 0;
    if (c_dcg_node_expr_bind((dcg_expression_node*) b2, 1, &bench->reads[3]->base) != DCG_OK) return 0;

    if (c_dcg_node_append(b2, long_, DCG_TRUE_CONDITION) != DCG_OK) return 0;
    if (c_dcg_node_append(b2, short_, DCG_FALSE_CONDITION) != DCG_OK) return 0;
    if (c_dcg_node_append(b1, b2, DCG_TRUE_CONDITION) != DCG_OK) return 0;
    if (c_dcg_node_append(b1, cancel, DCG_FALSE_CONDITION) != DCG_OK) return 0;
    if (c_dcg_node_append(b0, b1, DCG_TRUE_CONDITION) != DCG_OK) return 0;
    if (c_dcg_node_append(b0, clear, DCG_FALSE_CONDITION) != DCG_OK) return 0;
    if (c_dcg_node_append(&bench->root->base, b0, DCG_NO_CONDITION) != DCG_OK) return 0;

    return 1;
}

static void bench_teardown(bench_case* bench) {
    if (bench->root) c_dcg_node_teardown_root(&bench->root->base);
    if (bench->store) c_dcg_mapping_lgroup_free(bench->store);
}

static double bench_seconds(void) {
    struct timespec now;
    (void) clock_gettime(CLOCK_MONOTONIC, &now);
    return (double) now.tv_sec + (double) now.tv_nsec * 1e-9;
}

int main(void) {
    (void) printf("bench_c_eval - %s\n", DCG_EVAL_DIRECT_HOOKS ? "direct hooks (DCG_EVAL_DIRECT_HOOKS=1)" : "dispatch (DCG_EVAL_DIRECT_HOOKS=0)");

    bench_case cases[BENCH_GRAPHS];
    memset(cases, 0, sizeof(cases));
    for (size_t i = 0; i < BENCH_GRAPHS; i++) {
        if (!bench_build(&cases[i], i)) {
            (void) fprintf(stderr, "could not build graph %zu\n", i);
            return 1;
        }
    }

    /* A first pass, so the pages are warm and the first-call costs are not the
     * ones being reported. */
    volatile long checksum = 0;
    for (size_t i = 0; i < BENCH_GRAPHS; i++) {
        (void) c_dcg_root_node_eval(cases[i].root);
        checksum += (long) cases[i].root->eval_path.n_nodes;
    }

    /* The fastest of a few passes, not the average of one: on a shared machine
     * the noise between runs is larger than the difference being looked for, and
     * the least disturbed pass is the one worth comparing. */
    const size_t rounds = 20000;
    const size_t passes = 5;
    double       best   = 0.0;

    for (size_t pass = 0; pass < passes; pass++) {
        double started = bench_seconds();
        for (size_t round = 0; round < rounds; round++) {
            for (size_t i = 0; i < BENCH_GRAPHS; i++) {
                if (c_dcg_root_node_eval(cases[i].root) != DCG_OK) {
                    (void) fprintf(stderr, "graph %zu did not evaluate\n", i);
                    return 1;
                }
                /* Read the record back, so the walk cannot be optimised away
                 * without the numbers changing. */
                checksum += (long) cases[i].root->eval_path.leaf->ntype;
            }
        }
        double elapsed = bench_seconds() - started;
        if (best == 0.0 || elapsed < best) best = elapsed;
    }
    double elapsed = best;

    size_t evaluations = rounds * BENCH_GRAPHS;
    size_t nodes       = 0;
    size_t with_rule   = 0;
    for (size_t i = 0; i < BENCH_GRAPHS; i++) {
        nodes += c_dcg_node_subtree_size(&cases[i].root->base);
        /* How many of the nodes a walk reaches carry a rule of their own tells a
         * reader which build this was, and that the switch did something: the
         * reads are reached as operands, not as children, so they are counted
         * with the graph's own nodes only when they are walked. */
        for (dcg_node* node = &cases[i].root->base; node; node = node->children) {
            if (node->eval_ctx.type_eval_fn) with_rule++;
        }
    }

    (void) printf("graphs         : %d (%zu nodes each, %zu in all)\n", BENCH_GRAPHS, nodes / BENCH_GRAPHS, nodes);
    (void) printf("evaluations    : %zu (%zu passes, %zu rounds of %d, fastest pass)\n", evaluations * passes, passes, rounds, BENCH_GRAPHS);
    (void) printf("elapsed        : %.3f ms\n", elapsed * 1e3);
    (void) printf("per evaluation : %.1f ns\n", elapsed * 1e9 / (double) evaluations);
    (void) printf("per node       : %.1f ns\n", elapsed * 1e9 / ((double) evaluations * (double) (nodes / BENCH_GRAPHS)));
    (void) printf("with own rule  : %zu of %zu walked nodes\n", with_rule, nodes);
    (void) printf("checksum       : %ld\n", checksum);
    bench_ignore_harness();

    for (size_t i = 0; i < BENCH_GRAPHS; i++) bench_teardown(&cases[i]);
    return 0;
}
