/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "util/sstream.h"
#include "kernel/instantiate.h"
#include "kernel/inductive/inductive.h"
#include "library/util.h"
#include "library/constants.h"
#include "library/normalize.h"
#include "library/aux_recursors.h"
#include "library/compiler/util.h"
#include "library/compiler/comp_irrelevant.h"
#include "library/compiler/rec_fn_macro.h"
#include "library/compiler/compiler_step_visitor.h"

namespace lean {
static expr * g_neutral_expr     = nullptr;
static expr * g_unreachable_expr = nullptr;
class erase_irrelevant_fn : public compiler_step_visitor {

    virtual expr visit_sort(expr const &) override {
        return *g_neutral_expr;
    }

    virtual expr visit_pi(expr const &) override {
        return *g_neutral_expr;
    }

    virtual expr visit_macro(expr const & e) override {
        if (is_marked_as_comp_irrelevant(e))
            return *g_neutral_expr;
        else if (is_comp_irrelevant(ctx(), e))
            return *g_neutral_expr;
        else if (is_rec_fn_macro(e))
            return mk_constant(get_rec_fn_name(e));
        else
            return compiler_step_visitor::visit_macro(e);
    }

    virtual expr visit_local(expr const & e) override {
        if (is_comp_irrelevant(ctx(), e))
            return *g_neutral_expr;
        else
            return e;
    }

    virtual expr visit_constant(expr const & e) override {
        if (is_comp_irrelevant(ctx(), e)) {
            return *g_neutral_expr;
        } else {
            /* Erase universe level information */
            return mk_constant(const_name(e));
        }
    }

    expr erase_lambda_let_types(expr const & e) {
        if (is_lambda(e)) {
            return mk_lambda(binding_name(e), *g_neutral_expr, erase_lambda_let_types(binding_body(e)));
        } else if (is_let(e)) {
            return mk_let(let_name(e), *g_neutral_expr, let_value(e), erase_lambda_let_types(let_body(e)));
        } else {
            return e;
        }
    }

    virtual expr visit_lambda(expr const & e) override {
        return erase_lambda_let_types(compiler_step_visitor::visit_lambda(e));
    }

    virtual expr visit_let(expr const & e) override {
        return erase_lambda_let_types(compiler_step_visitor::visit_let(e));
    }

    /* Process minor premises and args, and distribute args over minors.
       The size of cnames is nminors, and it contains the names of the constructors. */
    void visit_minors(unsigned nparams, unsigned nminors, expr * minors, name const * cnames,
                      unsigned nargs, expr * args) {
        if (nargs > 0) {
            for (unsigned i = 0; i < nargs; i++)
                args[i] = visit(args[i]);
            /* distribute args over minors */
            for (unsigned i = 0; i < nminors; i++) {
                unsigned carity = get_constructor_arity(env(), cnames[i]);
                lean_assert(carity >= nparams);
                unsigned data_sz = carity - nparams;
                type_context::tmp_locals locals(ctx());
                expr new_minor   = minors[i];
                for (unsigned j = 0; j < data_sz; j++) {
                    if (!is_lambda(new_minor))
                        throw exception("unexpected occurrence of 'cases_on' expression, the minor premise is expected to be a lambda-expression");
                    expr local = locals.push_local_from_binding(new_minor);
                    new_minor  = instantiate(binding_body(new_minor), local);
                }
                new_minor = visit(new_minor);
                new_minor = beta_reduce(mk_app(new_minor, nargs, args));
                minors[i] = erase_lambda_let_types(locals.mk_lambda(new_minor));
            }
        } else {
            for (unsigned i = 0; i < nminors; i++)
                minors[i] = visit(minors[i]);
        }
    }

    /* We keep only the major and minor premises in cases_on applications. */
    expr visit_cases_on(expr const & fn, buffer<expr> & args) {
        name const & rec_name       = const_name(fn);
        name const & I_name         = rec_name.get_prefix();
        if (I_name == get_false_name())
            return *g_unreachable_expr;
        unsigned nparams            = *inductive::get_num_params(env(), I_name);
        unsigned nminors            = *inductive::get_num_minor_premises(env(), I_name);
        unsigned nindices           = *inductive::get_num_indices(env(), I_name);
        unsigned arity              = nparams + 1 /* typeformer/motive */ + nindices + 1 /* major premise */ + nminors;
        lean_assert(args.size() >= arity);
        buffer<name> cnames;
        get_intro_rule_names(env(), I_name, cnames);
        expr * minors        = args.data() + nparams + 1 + nindices + 1;
        unsigned nextra_args = args.size() - arity;
        expr * extra_args    = args.data() + arity;
        visit_minors(nparams, nminors, minors, cnames.data(), nextra_args, extra_args);
        expr major  = visit(args[nparams + 1 + nindices]);
        expr new_fn = visit(fn);
        return mk_app(mk_app(new_fn, major), nminors, minors);
    }

    /* We keep only the major and minor premises in rec applications.
       This method also converts the rec into cases_on */
    expr visit_rec(expr const & fn, buffer<expr> & args) {
        name const & rec_name       = const_name(fn);
        name const & I_name         = rec_name.get_prefix();
        if (I_name == get_false_name())
            return *g_unreachable_expr;
        /* This preprocessing step assumes that recursive recursors have already been eliminated */
        lean_assert(!is_recursive_datatype(env(), I_name));
        unsigned nparams            = *inductive::get_num_params(env(), I_name);
        unsigned nminors            = *inductive::get_num_minor_premises(env(), I_name);
        unsigned nindices           = *inductive::get_num_indices(env(), I_name);
        unsigned arity              = nparams + 1 /* typeformer/motive */ + nminors + nindices + 1 /* major premise */;
        lean_assert(args.size() >= arity);
        buffer<name> cnames;
        get_intro_rule_names(env(), I_name, cnames);
        expr new_fn = mk_constant(name(I_name, "cases_on"));
        expr major  = visit(args[nparams + 1 + nminors + nindices]);
        expr * minors        = args.data() + nparams + 1;
        unsigned nextra_args = args.size() - arity;
        expr * extra_args    = args.data() + arity;
        visit_minors(nparams, nminors, minors, cnames.data(), nextra_args, extra_args);
        return mk_app(mk_app(new_fn, major), nminors, minors);
    }

    expr add_args(expr e, unsigned start_idx, buffer<expr> const & args) {
        for (unsigned i = start_idx; i < args.size(); i++)
            e = mk_app(e, args[i]);
        return beta_reduce(e);
    }

    /* Remove eq.rec applications since they are just "type-casting" operations. */
    expr visit_eq_rec(buffer<expr> & args) {
        lean_assert(args.size() >= 6);
        expr major = visit(args[3]);
        return add_args(major, 6, args);
    }

    expr consume_lambdas(type_context::tmp_locals & locals, expr e) {
        while (true) {
            if (is_lambda(e)) {
                expr local = locals.push_local_from_binding(e);
                e = instantiate(binding_body(e), local);
            } else {
                return beta_reduce(e);
            }
        }
    }

    /* We can eliminate no_confusion applications, they do not add any relevant information to the environment */
    expr visit_no_confusion(expr const & fn, buffer<expr> const & args) {
        lean_assert(is_constant(fn));
        name const & no_confusion_name  = const_name(fn);
        name const & I_name             = no_confusion_name.get_prefix();
        unsigned nparams                = *inductive::get_num_params(env(), I_name);
        unsigned nindices               = *inductive::get_num_indices(env(), I_name);
        DEBUG_CODE(unsigned basic_arity = nparams + nindices + 1 /* motive */ + 2 /* lhs/rhs */ + 1 /* equality */;);
        lean_assert(args.size() >= basic_arity);
        expr lhs                        = ctx().whnf(args[nparams + nindices + 1]);
        expr rhs                        = ctx().whnf(args[nparams + nindices + 2]);
        optional<name> lhs_constructor  = is_constructor_app(env(), lhs);
        optional<name> rhs_constructor  = is_constructor_app(env(), rhs);
        if (!lhs_constructor || !rhs_constructor)
            throw exception(sstream() << "code generation failed, unsupported occurrence of '" << no_confusion_name << "', constructors expected");
        if (lhs_constructor != rhs_constructor)
            return *g_unreachable_expr;
        lean_assert(args.size() >= basic_arity + 1);
        expr major = args[nparams + nindices + 4];
        type_context::tmp_locals locals(ctx());
        major = consume_lambdas(locals, major);
        major = visit(major);
        major = erase_lambda_let_types(locals.mk_lambda(major));
        expr r = major;

        unsigned c_data_sz = get_constructor_arity(env(), *lhs_constructor) - nparams;
        for (unsigned i = 0; i < c_data_sz; i++)
            r = mk_app(r, *g_neutral_expr); // add dummy proofs
        /* add remaining arguments */
        return add_args(r, nparams + nindices + 5, args);
    }

    /* Treat subtype.tag as the identity function */
    virtual expr visit_subtype_tag(buffer<expr> const & args) {
        lean_assert(args.size() >= 4);
        expr r = visit(args[2]);
        return add_args(r, 4, args);
    }

    /* Eliminate subtype.rec */
    virtual expr visit_subtype_rec(buffer<expr> const & args) {
        lean_assert(args.size() >= 5);
        expr minor = visit(args[3]);
        expr major = visit(args[4]);
        expr r     = mk_app(minor, major, *g_neutral_expr);
        return add_args(r, 5, args);
    }

    /* subtype.elt_of is also compiled as the identity function */
    virtual expr visit_subtype_elt_of(buffer<expr> const & args) {
        lean_assert(args.size() >= 3);
        expr r = visit(args[2]);
        return add_args(r, 3, args);
    }

    virtual expr visit_app(expr const & e) override {
        if (is_comp_irrelevant(ctx(), e))
            return *g_neutral_expr;
        buffer<expr> args;
        expr const & fn = get_app_args(e, args);
        if (is_lambda(fn)) {
            return visit(beta_reduce(e));
        } else if (is_constant(fn)) {
            name const & n = const_name(fn);
            if (n == get_eq_rec_name()) {
                return visit_eq_rec(args);
            } else if (n == get_subtype_rec_name()) {
                return visit_subtype_rec(args);
            } else if (is_cases_on_recursor(env(), n)) {
                return visit_cases_on(fn, args);
            } else if (inductive::is_elim_rule(env(), n)) {
                return visit_rec(fn, args);
            } else if (is_no_confusion(env(), n)) {
                return visit_no_confusion(fn, args);
            } else if (n == get_subtype_tag_name()) {
                return visit_subtype_tag(args);
            } else if (n == get_subtype_elt_of_name()) {
                return visit_subtype_elt_of(args);
            }
        }
        return compiler_step_visitor::visit_app(e);
    }

public:
    erase_irrelevant_fn(environment const & env):compiler_step_visitor(env) {}
};

expr erase_irrelevant(environment const & env, expr const & e) {
    return erase_irrelevant_fn(env)(e);
}

bool is_neutral_expr(expr const & e) {
    return e == *g_neutral_expr;
}

bool is_unreachable_expr(expr const & e) {
    return e == *g_unreachable_expr;
}

void initialize_erase_irrelevant() {
    g_neutral_expr     = new expr(mk_constant("_neutral_"));
    g_unreachable_expr = new expr(mk_constant("_unreachable_"));
}

void finalize_erase_irrelevant() {
    delete g_neutral_expr;
    delete g_unreachable_expr;
}
}