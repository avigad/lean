/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <string>
#include <iostream>
#include "library/vm/vm.h"
#include "library/vm/vm_nat.h"
#include "library/vm/vm_string.h"
#include "library/vm/vm_ordering.h"

namespace lean {
struct vm_name : public vm_external {
    name m_val;
    vm_name(name const & v):m_val(v) {}
    virtual void dealloc() override { this->~vm_name(); get_vm_allocator().deallocate(sizeof(vm_name), this); }
};

name const & to_name(vm_obj const & o) {
    lean_assert(is_external(o));
    lean_assert(dynamic_cast<vm_name*>(to_external(o)));
    return static_cast<vm_name*>(to_external(o))->m_val;
}

vm_obj to_obj(name const & n) {
    return mk_vm_external(new (get_vm_allocator().allocate(sizeof(vm_name))) vm_name(n));
}

list<name> to_list_name(vm_obj const & o) {
    if (is_simple(o))
        return list<name>();
    else
        return list<name>(to_name(cfield(o, 0)), to_list_name(cfield(o, 1)));
}

void to_buffer_name(vm_obj const & o, buffer<name> & r) {
    if (is_simple(o)) {
        return;
    } else {
        r.push_back(to_name(cfield(o, 0)));
        to_buffer_name(cfield(o, 1), r);
    }
}

vm_obj to_obj(list<name> const & ls) {
    if (!ls)
        return mk_vm_simple(0);
    else
        return mk_vm_constructor(1, to_obj(head(ls)), to_obj(tail(ls)));
}

vm_obj name_anonymous() {
    return to_obj(name());
}

vm_obj name_mk_string(vm_obj const & s, vm_obj const & n) {
    std::string str = to_string(s);
    return to_obj(name(to_name(n), str.c_str()));
}

vm_obj name_mk_numeral(vm_obj const & num, vm_obj const & n) {
    return to_obj(name(to_name(n), to_unsigned(num)));
}

unsigned name_cases_on(vm_obj const & o, buffer<vm_obj> & data) {
    name const & n = to_name(o);
    if (n.is_anonymous()) {
        return 0;
    } else if (n.is_string()) {
        data.push_back(to_obj(std::string(n.get_string())));
        data.push_back(to_obj(n.get_prefix()));
        return 1;
    } else {
        data.push_back(mk_vm_nat(n.get_numeral()));
        data.push_back(to_obj(n.get_prefix()));
        return 2;
    }
}

vm_obj name_has_decidable_eq(vm_obj const & o1, vm_obj const & o2) {
    return mk_vm_bool(to_name(o1) == to_name(o2));
}

vm_obj name_cmp(vm_obj const & o1, vm_obj const & o2) {
    return int_to_ordering(quick_cmp(to_name(o1), to_name(o2)));
}

vm_obj name_lex_cmp(vm_obj const & o1, vm_obj const & o2) {
    return int_to_ordering(cmp(to_name(o1), to_name(o2)));
}

void initialize_vm_name() {
    DECLARE_VM_BUILTIN(name({"name", "anonymous"}),        name_anonymous);
    DECLARE_VM_BUILTIN(name({"name", "mk_string"}),        name_mk_string);
    DECLARE_VM_BUILTIN(name({"name", "mk_numeral"}),       name_mk_numeral);
    DECLARE_VM_BUILTIN(name({"name", "has_decidable_eq"}), name_has_decidable_eq);
    DECLARE_VM_BUILTIN(name({"name", "cmp"}),              name_cmp);
    DECLARE_VM_BUILTIN(name({"name", "lex_cmp"}),          name_lex_cmp);
    DECLARE_VM_CASES_BUILTIN(name({"name", "cases_on"}),   name_cases_on);
}

void finalize_vm_name() {
}
}