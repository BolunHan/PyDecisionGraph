/*
 * c_node.h - console rendering: tree layout, option toggles, depth limiting
 * and buffer-truncation reporting.
 */

#include <decision_graph/decision_tree/bake/c_node.h>

#include <decision_graph/decision_tree/bake/c_collection.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_expr.h>

#include <decision_graph/decision_tree/bake/c_action.h>

#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

/** A hook the renderer can report, so the hooks column has something to show. */
static int post_eval_stub(dcg_node* node, void* user_data) {
    (void) node;
    (void) user_data;
    return DCG_OK;
}

/** root -> {long action, short action}, a two-level tree. */
static dcg_node* build_tree(void) {
    dcg_node* root   = dcg_t_node_root("Entry Point");
    dcg_node* branch = dcg_t_node_binary(DCG_OP_LT, "x < 10");
    dcg_node* yes    = dcg_t_node_action(DCG_NODE_LONGACTION, "go long");
    dcg_node* no     = dcg_t_node_action(DCG_NODE_SHORTACTION, "go short");

    (void) c_dcg_node_append(root, branch, DCG_NO_CONDITION);
    (void) c_dcg_node_append_auto(branch, yes);
    (void) c_dcg_node_append_auto(branch, no);
    return root;
}

static void test_default_layout(void) {
    dcg_node* root = build_tree();
    char      out[4096];

    int       written = c_dcg_node_render_to_string(root, out, sizeof(out), NULL);
    DCG_CHECK(written > 0);

    /* Every node of the tree shows up, with its kind, repr and value. */
    DCG_CHECK_CONTAINS(out, "ROOT");
    DCG_CHECK_CONTAINS(out, "Entry Point");
    DCG_CHECK_CONTAINS(out, "BINARY \"x < 10\"");
    DCG_CHECK_CONTAINS(out, "x < 10");
    DCG_CHECK_CONTAINS(out, "LONGACTION");
    DCG_CHECK_CONTAINS(out, "go long");
    DCG_CHECK_CONTAINS(out, "SHORTACTION");
    DCG_CHECK_CONTAINS(out, "go short");

    /* The edges are labelled with the branch conditions (their reprs). */
    DCG_CHECK_CONTAINS(out, "[<CONDITION 0x");
    DCG_CHECK_CONTAINS(out, "(True)]");
    DCG_CHECK_CONTAINS(out, "(False)]");
    DCG_CHECK_CONTAINS(out, "(Unconditional)]");

    /* Children count and the value slot are part of the default layout. */
    DCG_CHECK_CONTAINS(out, "children=2");
    DCG_CHECK_CONTAINS(out, "out=");

    /* Box drawing: the root has no glyph, the branch is last, the leaves fan out. */
    DCG_CHECK_CONTAINS(out, "└── ");
    DCG_CHECK_CONTAINS(out, "├── ");

    /* One line per node. */
    int lines = 0;
    for (const char* walk = out; *walk; walk++) {
        if (*walk == '\n') lines++;
    }
    DCG_CHECK_INT(lines, 4);

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

static void test_ascii_style(void) {
    dcg_node*       root = build_tree();
    char            out[4096];

    dcg_render_opts opts;
    c_dcg_render_opts_default(&opts);
    opts.style = DCG_RENDER_ASCII;

    DCG_CHECK(c_dcg_node_render_to_string(root, out, sizeof(out), &opts) > 0);
    (void) printf("%s", out);
    DCG_CHECK_CONTAINS(out, "`-- ");
    DCG_CHECK_CONTAINS(out, "|-- ");
    DCG_CHECK(!strstr(out, "└──"));

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

static void test_option_toggles(void) {
    dcg_node*       root = build_tree();
    char            out[4096];

    dcg_render_opts opts;
    c_dcg_render_opts_default(&opts);

    /* A bare layout: kinds and reprs only. */
    opts.show_condition = false;
    opts.show_out       = false;
    opts.show_children  = false;
    DCG_CHECK(c_dcg_node_render_to_string(root, out, sizeof(out), &opts) > 0);
    (void) printf("%s", out);
    DCG_CHECK(!strstr(out, "children="));
    DCG_CHECK(!strstr(out, "out="));
    DCG_CHECK(!strstr(out, "<CONDITION"));
    DCG_CHECK_CONTAINS(out, "BINARY \"x < 10\"");

    /* Identity and bindings on demand. */
    c_dcg_render_opts_default(&opts);
    opts.show_uid          = true;
    opts.show_flags        = true;
    opts.show_hooks        = true;
    opts.show_address      = true;
    root->eval_ctx.eval_fn = post_eval_stub;
    DCG_CHECK(c_dcg_node_render_to_string(root, out, sizeof(out), &opts) > 0);
    (void) printf("%s", out);
    DCG_CHECK_CONTAINS(out, "uid=");
    DCG_CHECK_CONTAINS(out, "hooks=[");
    DCG_CHECK_CONTAINS(out, "@0x");

    /* Labels appear only when asked for. */
    c_dcg_render_opts_default(&opts);
    DCG_CHECK_INT(c_dcg_node_add_label(root, "entry"), DCG_OK);
    DCG_CHECK(c_dcg_node_render_to_string(root, out, sizeof(out), &opts) > 0);
    (void) printf("%s", out);
    DCG_CHECK(!strstr(out, "labels="));

    opts.show_labels = true;
    DCG_CHECK(c_dcg_node_render_to_string(root, out, sizeof(out), &opts) > 0);
    (void) printf("%s", out);
    DCG_CHECK_CONTAINS(out, "labels=[entry]");

    /* A frozen node says so through its flags; the repr it renders is the copy
     * the node took at init, taken again here through the setter. */
    c_dcg_render_opts_default(&opts);
    opts.show_flags = true;
    root->flags |= DCG_NODE_FLAG_FROZEN;
    DCG_CHECK_INT(c_dcg_node_set_repr(root, "Entry Point"), DCG_OK);
    DCG_CHECK(c_dcg_node_render_to_string(root, out, sizeof(out), &opts) > 0);
    (void) printf("%s", out);
    DCG_CHECK_CONTAINS(out, "flags=frozen");
    DCG_CHECK_CONTAINS(out, "Entry Point");

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

static void test_depth_limit(void) {
    dcg_node*       root = build_tree();
    char            out[4096];

    dcg_render_opts opts;
    c_dcg_render_opts_default(&opts);
    opts.max_depth = 1;

    DCG_CHECK(c_dcg_node_render_to_string(root, out, sizeof(out), &opts) > 0);
    (void) printf("%s", out);
    DCG_CHECK_CONTAINS(out, "ROOT");
    DCG_CHECK_CONTAINS(out, "BINARY \"x < 10\"");
    /* The leaves are hidden, and the renderer says how many. */
    DCG_CHECK(!strstr(out, "LONGACTION"));
    DCG_CHECK_CONTAINS(out, "children hidden");

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

static void test_stream_and_truncation(void) {
    dcg_node* root = build_tree();
    char      small[64];

    /* A buffer that cannot hold the tree reports the loss instead of
     * silently returning half a tree. */
    DCG_CHECK_INT(c_dcg_node_render_to_string(root, small, sizeof(small), NULL), DCG_ERR_FULL);
    DCG_CHECK(small[sizeof(small) - 1] == '\0');

    /* The stream renderer returns the character count. */
    FILE* stream = tmpfile();
    DCG_CHECK(stream != NULL);
    if (stream) {
        DCG_CHECK(c_dcg_node_render(root, stream, NULL) > 0);
        (void) fclose(stream);
    }

    /* NULL-safety. */
    DCG_CHECK_INT(c_dcg_node_render(NULL, stdout, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_render(root, NULL, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_render_to_string(NULL, small, sizeof(small), NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_render_to_string(root, NULL, sizeof(small), NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_render_to_string(root, small, 0, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_print(NULL), DCG_ERR_INVALID_ARG);

    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, branch, yes, no */
}

static void test_single_node_and_deep_tree(void) {
    /* A lone node renders as one root line. */
    dcg_node* single = dcg_t_node_double("lonely", 1.5);
    char      out[512];
    DCG_CHECK(c_dcg_node_render_to_string(single, out, sizeof(out), NULL) > 0);
    DCG_CHECK_CONTAINS(out, "DOUBLE");
    DCG_CHECK_CONTAINS(out, "lonely");
    DCG_CHECK(!strstr(out, "└──"));
    c_dcg_node_free(single);

    /* A deep chain exercises the recursion without blowing the prefix
     * buffer: the layout stays correct at every level. */
    enum { DEPTH = 200 };
    dcg_node* root = dcg_t_node_root("Entry Point");
    dcg_node* walk = root;
    for (int level = 0; level < DEPTH; level++) {
        dcg_node* child = dcg_t_node_collection(DCG_NODE_LIST, "level");
        (void) c_dcg_node_append(walk, child, level == 0 ? DCG_NO_CONDITION : DCG_TRUE_CONDITION);
        walk = child;
    }
    DCG_CHECK_INT(c_dcg_node_depth(walk), DEPTH);
    DCG_CHECK_INT(c_dcg_node_height(root), DEPTH);

    static char deep[256 * 1024];
    DCG_CHECK(c_dcg_node_render_to_string(root, deep, sizeof(deep), NULL) > 0);
    DCG_CHECK_CONTAINS(deep, "Entry Point");

    /* The teardown walks 201 levels without recursing, so a chain this deep
     * costs it nothing but the record. */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), DEPTH + 1);
}

int main(void) {
    (void) printf("test_c_node_render\n");
    DCG_RUN(test_default_layout);
    DCG_RUN(test_ascii_style);
    DCG_RUN(test_option_toggles);
    DCG_RUN(test_depth_limit);
    DCG_RUN(test_stream_and_truncation);
    DCG_RUN(test_single_node_and_deep_tree);
    DCG_SUMMARY("test_c_node_render");
    return dcg_test_failures == 0 ? 0 : 1;
}
