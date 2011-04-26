/*
 * ctf-visitor-generate-io-struct.c
 *
 * Common Trace Format Metadata Visitor (generate I/O structures).
 *
 * Copyright 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <glib.h>
#include <inttypes.h>
#include <endian.h>
#include <errno.h>
#include <babeltrace/list.h>
#include <babeltrace/types.h>
#include <babeltrace/ctf/metadata.h>
#include <uuid/uuid.h>
#include "ctf-scanner.h"
#include "ctf-parser.h"
#include "ctf-ast.h"

#define fprintf_dbg(fd, fmt, args...)	fprintf(fd, "%s: " fmt, __func__, ## args)

#define _cds_list_first_entry(ptr, type, member)	\
	cds_list_entry((ptr)->next, type, member)

static
struct declaration *ctf_declaration_specifier_visit(FILE *fd,
		int depth, struct cds_list_head *head,
		struct declaration_scope *declaration_scope,
		struct ctf_trace *trace);

/*
 * String returned must be freed by the caller using g_free.
 */
static
char *concatenate_unary_strings(struct cds_list_head *head)
{
	struct ctf_node *node;
	GString *str;
	int i = 0;

	str = g_string_new("");
	cds_list_for_each_entry(node, head, siblings) {
		char *src_string;

		assert(node->type == NODE_UNARY_EXPRESSION);
		assert(node->u.unary_expression.type == UNARY_STRING);
		assert((node->u.unary_expression.link == UNARY_LINK_UNKNOWN)
			^ (i != 0));
		switch (node->u.unary_expression.link) {
		case UNARY_DOTLINK:
			g_string_append(str, ".");
			break;
		case UNARY_ARROWLINK:
			g_string_append(str, "->");
			break;
		case UNARY_DOTDOTDOT:
			g_string_append(str, "...");
			break;
		default:
			break;
		}
		src_string = node->u.unary_expression.u.string;
		g_string_append(str, src_string);
		i++;
	}
	return g_string_free(str, FALSE);
}

static
int get_unary_unsigned(struct cds_list_head *head, uint64_t *value)
{
	struct ctf_node *node;
	int i = 0;

	cds_list_for_each_entry(node, head, siblings) {
		assert(node->type == NODE_UNARY_EXPRESSION);
		assert(node->u.unary_expression.type == UNARY_UNSIGNED_CONSTANT);
		assert(node->u.unary_expression.link == UNARY_LINK_UNKNOWN);
		assert(i == 0);
		*value = node->u.unary_expression.u.unsigned_constant;
		i++;
	}
	return 0;
}

static
int get_unary_uuid(struct cds_list_head *head, uuid_t *uuid)
{
	struct ctf_node *node;
	int i = 0;
	int ret = -1;

	cds_list_for_each_entry(node, head, siblings) {
		const char *src_string;

		assert(node->type == NODE_UNARY_EXPRESSION);
		assert(node->u.unary_expression.type == UNARY_STRING);
		assert(node->u.unary_expression.link == UNARY_LINK_UNKNOWN);
		assert(i == 0);
		src_string = node->u.unary_expression.u.string;
		ret = uuid_parse(node->u.unary_expression.u.string, *uuid);
	}
	return ret;
}

static
struct ctf_stream *trace_stream_lookup(struct ctf_trace *trace, uint64_t stream_id)
{
	if (trace->streams->len <= stream_id)
		return NULL;
	return g_ptr_array_index(trace->streams, stream_id);
}

static
int visit_declaration_specifier(struct cds_list_head *declaration_specifier, GString *str)
{
	struct ctf_node *iter;
	int alias_item_nr = 0;
	int err;

	cds_list_for_each_entry(iter, declaration_specifier, siblings) {
		if (alias_item_nr != 0)
			g_string_append(str, " ");
		alias_item_nr++;

		switch (iter->type) {
		case NODE_TYPE_SPECIFIER:
			switch (iter->u.type_specifier.type) {
			case TYPESPEC_VOID:
				g_string_append(str, "void");
				break;
			case TYPESPEC_CHAR:
				g_string_append(str, "char");
				break;
			case TYPESPEC_SHORT:
				g_string_append(str, "short");
				break;
			case TYPESPEC_INT:
				g_string_append(str, "int");
				break;
			case TYPESPEC_LONG:
				g_string_append(str, "long");
				break;
			case TYPESPEC_FLOAT:
				g_string_append(str, "float");
				break;
			case TYPESPEC_DOUBLE:
				g_string_append(str, "double");
				break;
			case TYPESPEC_SIGNED:
				g_string_append(str, "signed");
				break;
			case TYPESPEC_UNSIGNED:
				g_string_append(str, "unsigned");
				break;
			case TYPESPEC_BOOL:
				g_string_append(str, "bool");
				break;
			case TYPESPEC_COMPLEX:
				g_string_append(str, "_Complex");
				break;
			case TYPESPEC_IMAGINARY:
				g_string_append(str, "_Imaginary");
				break;
			case TYPESPEC_CONST:
				g_string_append(str, "const");
				break;
			case TYPESPEC_ID_TYPE:
				if (iter->u.type_specifier.id_type)
					g_string_append(str, iter->u.type_specifier.id_type);
				break;
			default:
				fprintf(stderr, "[error] %s: unknown specifier\n", __func__);
				err = -EINVAL;
				goto error;
			}
			break;
		case NODE_ENUM:
			if (!iter->u._enum.enum_id) {
				fprintf(stderr, "[error] %s: unexpected empty enum ID\n", __func__);
				err = -EINVAL;
				goto error;
			}
			g_string_append(str, "enum ");
			g_string_append(str, iter->u._enum.enum_id);
			break;
		case NODE_VARIANT:
			if (!iter->u.variant.name) {
				fprintf(stderr, "[error] %s: unexpected empty variant name\n", __func__);
				err = -EINVAL;
				goto error;
			}
			g_string_append(str, "variant ");
			g_string_append(str, iter->u.variant.name);
			break;
		case NODE_STRUCT:
			if (!iter->u._struct.name) {
				fprintf(stderr, "[error] %s: unexpected empty variant name\n", __func__);
				err = -EINVAL;
				goto error;
			}
			g_string_append(str, "struct ");
			g_string_append(str, iter->u._struct.name);
			break;
		default:
			fprintf(stderr, "[error] %s: unexpected node type %d\n", __func__, (int) iter->type);
			err = -EINVAL;
			goto error;
		}
	}
	return 0;
error:
	return err;
}

static
GQuark create_typealias_identifier(FILE *fd, int depth,
	struct cds_list_head *declaration_specifier,
	struct ctf_node *node_type_declarator)
{
	struct ctf_node *iter;
	GString *str;
	char *str_c;
	GQuark alias_q;
	int ret;

	str = g_string_new("");
	ret = visit_declaration_specifier(declaration_specifier, str);
	if (ret) {
		g_string_free(str, TRUE);
		return 0;
	}
	cds_list_for_each_entry(iter, &node_type_declarator->u.type_declarator.pointers, siblings) {
		g_string_append(str, " *");
		if (iter->u.pointer.const_qualifier)
			g_string_append(str, " const");
	}
	str_c = g_string_free(str, FALSE);
	alias_q = g_quark_from_string(str_c);
	g_free(str_c);
	return alias_q;
}

static
struct declaration *ctf_type_declarator_visit(FILE *fd, int depth,
	struct cds_list_head *declaration_specifier,
	GQuark *field_name,
	struct ctf_node *node_type_declarator,
	struct declaration_scope *declaration_scope,
	struct declaration *nested_declaration,
	struct ctf_trace *trace)
{
	/*
	 * Visit type declarator by first taking care of sequence/array
	 * (recursively). Then, when we get to the identifier, take care
	 * of pointers.
	 */

	if (node_type_declarator) {
		assert(node_type_declarator->u.type_declarator.type != TYPEDEC_UNKNOWN);

		/* TODO: gcc bitfields not supported yet. */
		if (node_type_declarator->u.type_declarator.bitfield_len != NULL) {
			fprintf(stderr, "[error] %s: gcc bitfields are not supported yet.\n", __func__);
			return NULL;
		}
	}

	if (!nested_declaration) {
		if (node_type_declarator && !cds_list_empty(&node_type_declarator->u.type_declarator.pointers)) {
			GQuark alias_q;

			/*
			 * If we have a pointer declarator, it _has_ to be present in
			 * the typealiases (else fail).
			 */
			alias_q = create_typealias_identifier(fd, depth,
				declaration_specifier, node_type_declarator);
			nested_declaration = lookup_declaration(alias_q, declaration_scope);
			if (!nested_declaration) {
				fprintf(stderr, "[error] %s: cannot find typealias \"%s\".\n", __func__, g_quark_to_string(alias_q));
				return NULL;
			}
		} else {
			nested_declaration = ctf_declaration_specifier_visit(fd, depth,
				declaration_specifier, declaration_scope, trace);
		}
	}

	if (!node_type_declarator)
		return nested_declaration;

	if (node_type_declarator->u.type_declarator.type == TYPEDEC_ID) {
		if (node_type_declarator->u.type_declarator.u.id)
			*field_name = g_quark_from_string(node_type_declarator->u.type_declarator.u.id);
		else
			*field_name = 0;
		return nested_declaration;
	} else {
		struct declaration *declaration;
		struct cds_list_head *length;
		struct ctf_node *first;

		/* TYPEDEC_NESTED */

		/* create array/sequence, pass nested_declaration as child. */
		length = &node_type_declarator->u.type_declarator.u.nested.length;
		if (cds_list_empty(length)) {
			fprintf(stderr, "[error] %s: expecting length type or value.\n", __func__);
			return NULL;
		}
		first = _cds_list_first_entry(length, struct ctf_node, siblings);
		switch (first->type) {
		case NODE_UNARY_EXPRESSION:
		{
			struct declaration_array *array_declaration;
			size_t len;

			if (first->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT) {
				fprintf(stderr, "[error] %s: array: unexpected unary expression.\n", __func__);
				return NULL;
			}
			len = first->u.unary_expression.u.unsigned_constant;
			array_declaration = array_declaration_new(len, nested_declaration,
						declaration_scope);
			declaration = &array_declaration->p;
			break;
		}
		case NODE_INTEGER:
		case NODE_TYPE_SPECIFIER:
		{
			struct declaration_sequence *sequence_declaration;
			struct declaration_integer *integer_declaration;
			GQuark dummy_id;

			declaration = ctf_type_declarator_visit(fd, depth,
						length,
						&dummy_id, NULL,
						declaration_scope,
						NULL, trace);
			assert(declaration->id == CTF_TYPE_INTEGER);
			integer_declaration = container_of(declaration, struct declaration_integer, p);
			sequence_declaration = sequence_declaration_new(integer_declaration,
					nested_declaration, declaration_scope);
			declaration = &sequence_declaration->p;
			break;
		}
		default:
			assert(0);
		}

		/* Pass it as content of outer container */
		declaration = ctf_type_declarator_visit(fd, depth,
				declaration_specifier, field_name,
				node_type_declarator->u.type_declarator.u.nested.type_declarator,
				declaration_scope, declaration, trace);
		return declaration;
	}
}

static
int ctf_struct_type_declarators_visit(FILE *fd, int depth,
	struct declaration_struct *struct_declaration,
	struct cds_list_head *declaration_specifier,
	struct cds_list_head *type_declarators,
	struct declaration_scope *declaration_scope,
	struct ctf_trace *trace)
{
	struct ctf_node *iter;
	GQuark field_name;

	cds_list_for_each_entry(iter, type_declarators, siblings) {
		struct declaration *field_declaration;

		field_declaration = ctf_type_declarator_visit(fd, depth,
						declaration_specifier,
						&field_name, iter,
						struct_declaration->scope,
						NULL, trace);
		struct_declaration_add_field(struct_declaration,
					     g_quark_to_string(field_name),
					     field_declaration);
	}
	return 0;
}

static
int ctf_variant_type_declarators_visit(FILE *fd, int depth,
	struct declaration_untagged_variant *untagged_variant_declaration,
	struct cds_list_head *declaration_specifier,
	struct cds_list_head *type_declarators,
	struct declaration_scope *declaration_scope,
	struct ctf_trace *trace)
{
	struct ctf_node *iter;
	GQuark field_name;

	cds_list_for_each_entry(iter, type_declarators, siblings) {
		struct declaration *field_declaration;

		field_declaration = ctf_type_declarator_visit(fd, depth,
						declaration_specifier,
						&field_name, iter,
						untagged_variant_declaration->scope,
						NULL, trace);
		untagged_variant_declaration_add_field(untagged_variant_declaration,
					      g_quark_to_string(field_name),
					      field_declaration);
	}
	return 0;
}

static
int ctf_typedef_visit(FILE *fd, int depth, struct declaration_scope *scope,
		struct cds_list_head *declaration_specifier,
		struct cds_list_head *type_declarators,
		struct ctf_trace *trace)
{
	struct ctf_node *iter;
	GQuark identifier;

	cds_list_for_each_entry(iter, type_declarators, siblings) {
		struct declaration *type_declaration;
		int ret;
	
		type_declaration = ctf_type_declarator_visit(fd, depth,
					declaration_specifier,
					&identifier, iter,
					scope, NULL, trace);
		ret = register_declaration(identifier, type_declaration, scope);
		if (ret) {
			type_declaration->declaration_free(type_declaration);
			return ret;
		}
	}
	return 0;
}

static
int ctf_typealias_visit(FILE *fd, int depth, struct declaration_scope *scope,
		struct ctf_node *target, struct ctf_node *alias,
		struct ctf_trace *trace)
{
	struct declaration *type_declaration;
	struct ctf_node *node;
	GQuark dummy_id;
	GQuark alias_q;
	int err;

	/* See ctf_visitor_type_declarator() in the semantic validator. */

	/*
	 * Create target type declaration.
	 */

	if (cds_list_empty(&alias->u.typealias_target.type_declarators))
		node = NULL;
	else
		node = _cds_list_first_entry(&alias->u.typealias_target.type_declarators,
				struct ctf_node, siblings);
	type_declaration = ctf_type_declarator_visit(fd, depth,
		&target->u.typealias_target.declaration_specifier,
		&dummy_id, node,
		scope, NULL, trace);
	if (!type_declaration) {
		fprintf(stderr, "[error] %s: problem creating type declaration\n", __func__);
		err = -EINVAL;
		goto error;
	}
	/*
	 * The semantic validator does not check whether the target is
	 * abstract or not (if it has an identifier). Check it here.
	 */
	if (dummy_id != 0) {
		fprintf(stderr, "[error] %s: expecting empty identifier\n", __func__);
		err = -EINVAL;
		goto error;
	}
	/*
	 * Create alias identifier.
	 */

	node = _cds_list_first_entry(&alias->u.typealias_alias.type_declarators,
				struct ctf_node, siblings);
	alias_q = create_typealias_identifier(fd, depth,
			&alias->u.typealias_alias.declaration_specifier, node);
	err = register_declaration(alias_q, type_declaration, scope);
	if (err)
		goto error;
	return 0;

error:
	type_declaration->declaration_free(type_declaration);
	return err;
}

static
int ctf_struct_declaration_list_visit(FILE *fd, int depth,
	struct ctf_node *iter, struct declaration_struct *struct_declaration,
	struct ctf_trace *trace)
{
	int ret;

	switch (iter->type) {
	case NODE_TYPEDEF:
		/* For each declarator, declare type and add type to struct declaration scope */
		ret = ctf_typedef_visit(fd, depth,
			struct_declaration->scope,
			&iter->u._typedef.declaration_specifier,
			&iter->u._typedef.type_declarators, trace);
		if (ret)
			return ret;
		break;
	case NODE_TYPEALIAS:
		/* Declare type with declarator and add type to struct declaration scope */
		ret = ctf_typealias_visit(fd, depth,
			struct_declaration->scope,
			iter->u.typealias.target,
			iter->u.typealias.alias, trace);
		if (ret)
			return ret;
		break;
	case NODE_STRUCT_OR_VARIANT_DECLARATION:
		/* Add field to structure declaration */
		ret = ctf_struct_type_declarators_visit(fd, depth,
				struct_declaration,
				&iter->u.struct_or_variant_declaration.declaration_specifier,
				&iter->u.struct_or_variant_declaration.type_declarators,
				struct_declaration->scope, trace);
		if (ret)
			return ret;
		break;
	default:
		fprintf(stderr, "[error] %s: unexpected node type %d\n", __func__, (int) iter->type);
		assert(0);
	}
	return 0;
}

static
int ctf_variant_declaration_list_visit(FILE *fd, int depth,
	struct ctf_node *iter,
	struct declaration_untagged_variant *untagged_variant_declaration,
	struct ctf_trace *trace)
{
	int ret;

	switch (iter->type) {
	case NODE_TYPEDEF:
		/* For each declarator, declare type and add type to variant declaration scope */
		ret = ctf_typedef_visit(fd, depth,
			untagged_variant_declaration->scope,
			&iter->u._typedef.declaration_specifier,
			&iter->u._typedef.type_declarators, trace);
		if (ret)
			return ret;
		break;
	case NODE_TYPEALIAS:
		/* Declare type with declarator and add type to variant declaration scope */
		ret = ctf_typealias_visit(fd, depth,
			untagged_variant_declaration->scope,
			iter->u.typealias.target,
			iter->u.typealias.alias, trace);
		if (ret)
			return ret;
		break;
	case NODE_STRUCT_OR_VARIANT_DECLARATION:
		/* Add field to structure declaration */
		ret = ctf_variant_type_declarators_visit(fd, depth,
				untagged_variant_declaration,
				&iter->u.struct_or_variant_declaration.declaration_specifier,
				&iter->u.struct_or_variant_declaration.type_declarators,
				untagged_variant_declaration->scope, trace);
		if (ret)
			return ret;
		break;
	default:
		fprintf(stderr, "[error] %s: unexpected node type %d\n", __func__, (int) iter->type);
		assert(0);
	}
	return 0;
}

static
struct declaration *ctf_declaration_struct_visit(FILE *fd,
	int depth, const char *name, struct cds_list_head *declaration_list,
	int has_body, struct declaration_scope *declaration_scope,
	struct ctf_trace *trace)
{
	struct declaration_struct *struct_declaration;
	struct ctf_node *iter;
	int ret;

	/*
	 * For named struct (without body), lookup in
	 * declaration scope. Don't take reference on struct
	 * declaration: ref is only taken upon definition.
	 */
	if (!has_body) {
		assert(name);
		struct_declaration =
			lookup_struct_declaration(g_quark_from_string(name),
						  declaration_scope);
		return &struct_declaration->p;
	} else {
		/* For unnamed struct, create type */
		/* For named struct (with body), create type and add to declaration scope */
		if (name) {
			if (lookup_struct_declaration(g_quark_from_string(name),
						      declaration_scope)) {
				
				fprintf(stderr, "[error] %s: struct %s already declared in scope\n", __func__, name);
				return NULL;
			}
		}
		struct_declaration = struct_declaration_new(declaration_scope);
		cds_list_for_each_entry(iter, declaration_list, siblings) {
			ret = ctf_struct_declaration_list_visit(fd, depth + 1, iter,
				struct_declaration, trace);
			if (ret)
				goto error;
		}
		if (name) {
			ret = register_struct_declaration(g_quark_from_string(name),
					struct_declaration,
					declaration_scope);
			assert(!ret);
		}
		return &struct_declaration->p;
	}
error:
	struct_declaration->p.declaration_free(&struct_declaration->p);
	return NULL;
}

static
struct declaration *ctf_declaration_variant_visit(FILE *fd,
	int depth, const char *name, const char *choice,
	struct cds_list_head *declaration_list,
	int has_body, struct declaration_scope *declaration_scope,
	struct ctf_trace *trace)
{
	struct declaration_untagged_variant *untagged_variant_declaration;
	struct declaration_variant *variant_declaration;
	struct ctf_node *iter;
	int ret;

	/*
	 * For named variant (without body), lookup in
	 * declaration scope. Don't take reference on variant
	 * declaration: ref is only taken upon definition.
	 */
	if (!has_body) {
		assert(name);
		untagged_variant_declaration =
			lookup_variant_declaration(g_quark_from_string(name),
						   declaration_scope);
	} else {
		/* For unnamed variant, create type */
		/* For named variant (with body), create type and add to declaration scope */
		if (name) {
			if (lookup_variant_declaration(g_quark_from_string(name),
						       declaration_scope)) {
				
				fprintf(stderr, "[error] %s: variant %s already declared in scope\n", __func__, name);
				return NULL;
			}
		}
		untagged_variant_declaration = untagged_variant_declaration_new(declaration_scope);
		cds_list_for_each_entry(iter, declaration_list, siblings) {
			ret = ctf_variant_declaration_list_visit(fd, depth + 1, iter,
				untagged_variant_declaration, trace);
			if (ret)
				goto error;
		}
		if (name) {
			ret = register_variant_declaration(g_quark_from_string(name),
					untagged_variant_declaration,
					declaration_scope);
			assert(!ret);
		}
	}
	/*
	 * if tagged, create tagged variant and return. else return
	 * untagged variant.
	 */
	if (!choice) {
		return &untagged_variant_declaration->p;
	} else {
		variant_declaration = variant_declaration_new(untagged_variant_declaration, choice);
		if (!variant_declaration)
			goto error;
		declaration_unref(&untagged_variant_declaration->p);
		return &variant_declaration->p;
	}
error:
	untagged_variant_declaration->p.declaration_free(&variant_declaration->p);
	return NULL;
}

static
int ctf_enumerator_list_visit(FILE *fd, int depth,
		struct ctf_node *enumerator,
		struct declaration_enum *enum_declaration)
{
	GQuark q;
	struct ctf_node *iter;

	q = g_quark_from_string(enumerator->u.enumerator.id);
	if (enum_declaration->integer_declaration->signedness) {
		int64_t start, end;
		int nr_vals = 0;

		cds_list_for_each_entry(iter, &enumerator->u.enumerator.values, siblings) {
			int64_t *target;

			assert(iter->type == NODE_UNARY_EXPRESSION);
			if (nr_vals == 0)
				target = &start;
			else
				target = &end;

			switch (iter->u.unary_expression.type) {
			case UNARY_SIGNED_CONSTANT:
				*target = iter->u.unary_expression.u.signed_constant;
				break;
			case UNARY_UNSIGNED_CONSTANT:
				*target = iter->u.unary_expression.u.unsigned_constant;
				break;
			default:
				fprintf(stderr, "[error] %s: invalid enumerator\n", __func__);
				return -EINVAL;
			}
			if (nr_vals > 1) {
				fprintf(stderr, "[error] %s: invalid enumerator\n", __func__);
				return -EINVAL;
			}
			nr_vals++;
		}
		if (nr_vals == 1)
			end = start;
		enum_signed_insert(enum_declaration, start, end, q);
	} else {
		uint64_t start, end;
		int nr_vals = 0;

		cds_list_for_each_entry(iter, &enumerator->u.enumerator.values, siblings) {
			uint64_t *target;

			assert(iter->type == NODE_UNARY_EXPRESSION);
			if (nr_vals == 0)
				target = &start;
			else
				target = &end;

			switch (iter->u.unary_expression.type) {
			case UNARY_UNSIGNED_CONSTANT:
				*target = iter->u.unary_expression.u.unsigned_constant;
				break;
			case UNARY_SIGNED_CONSTANT:
				/*
				 * We don't accept signed constants for enums with unsigned
				 * container type.
				 */
				fprintf(stderr, "[error] %s: invalid enumerator (signed constant encountered, but enum container type is unsigned)\n", __func__);
				return -EINVAL;
			default:
				fprintf(stderr, "[error] %s: invalid enumerator\n", __func__);
				return -EINVAL;
			}
			if (nr_vals > 1) {
				fprintf(stderr, "[error] %s: invalid enumerator\n", __func__);
				return -EINVAL;
			}
			nr_vals++;
		}
		if (nr_vals == 1)
			end = start;
		enum_unsigned_insert(enum_declaration, start, end, q);
	}
	return 0;
}

static
struct declaration *ctf_declaration_enum_visit(FILE *fd, int depth,
			const char *name,
			struct cds_list_head *container_type,
			struct cds_list_head *enumerator_list,
			int has_body,
			struct declaration_scope *declaration_scope,
			struct ctf_trace *trace)
{
	struct declaration *declaration;
	struct declaration_enum *enum_declaration;
	struct declaration_integer *integer_declaration;
	struct ctf_node *iter, *first;
	GQuark dummy_id;
	int ret;

	/*
	 * For named enum (without body), lookup in
	 * declaration scope. Don't take reference on enum
	 * declaration: ref is only taken upon definition.
	 */
	if (!has_body) {
		assert(name);
		enum_declaration =
			lookup_enum_declaration(g_quark_from_string(name),
						declaration_scope);
		return &enum_declaration->p;
	} else {
		/* For unnamed enum, create type */
		/* For named enum (with body), create type and add to declaration scope */
		if (name) {
			if (lookup_enum_declaration(g_quark_from_string(name),
						    declaration_scope)) {
				
				fprintf(stderr, "[error] %s: enum %s already declared in scope\n", __func__, name);
				return NULL;
			}
		}
		if (cds_list_empty(container_type)) {
				fprintf(stderr, "[error] %s: missing container type for enumeration\n", __func__);
				return NULL;
			
		}
		first = _cds_list_first_entry(container_type, struct ctf_node, siblings);
		switch (first->type) {
		case NODE_INTEGER:
		case NODE_TYPE_SPECIFIER:
			declaration = ctf_type_declarator_visit(fd, depth,
						container_type,
						&dummy_id, NULL,
						declaration_scope,
						NULL, trace);
			assert(declaration->id == CTF_TYPE_INTEGER);
			integer_declaration = container_of(declaration, struct declaration_integer, p);
			break;
		default:
			assert(0);
		}
		enum_declaration = enum_declaration_new(integer_declaration);
		declaration_unref(&integer_declaration->p);	/* leave ref to enum */
		cds_list_for_each_entry(iter, enumerator_list, siblings) {
			ret = ctf_enumerator_list_visit(fd, depth + 1, iter, enum_declaration);
			if (ret)
				goto error;
		}
		if (name) {
			ret = register_enum_declaration(g_quark_from_string(name),
					enum_declaration,
					declaration_scope);
			assert(!ret);
		}
		return &enum_declaration->p;
	}
error:
	enum_declaration->p.declaration_free(&enum_declaration->p);
	return NULL;
}

static
struct declaration *ctf_declaration_type_specifier_visit(FILE *fd, int depth,
		struct cds_list_head *declaration_specifier,
		struct declaration_scope *declaration_scope)
{
	GString *str;
	struct declaration *declaration;
	char *str_c;
	int ret;
	GQuark id_q;

	str = g_string_new("");
	ret = visit_declaration_specifier(declaration_specifier, str);
	if (ret)
		return NULL;
	str_c = g_string_free(str, FALSE);
	id_q = g_quark_from_string(str_c);
	g_free(str_c);
	declaration = lookup_declaration(id_q, declaration_scope);
	return declaration;
}

/*
 * Returns 0/1 boolean, or < 0 on error.
 */
static
int get_boolean(FILE *fd, int depth, struct ctf_node *unary_expression)
{
	if (unary_expression->type != NODE_UNARY_EXPRESSION) {
		fprintf(stderr, "[error] %s: expecting unary expression\n",
			__func__);
		return -EINVAL;
	}
	switch (unary_expression->u.unary_expression.type) {
	case UNARY_UNSIGNED_CONSTANT:
		if (unary_expression->u.unary_expression.u.unsigned_constant == 0)
			return 0;
		else
			return 1;
	case UNARY_SIGNED_CONSTANT:
		if (unary_expression->u.unary_expression.u.signed_constant == 0)
			return 0;
		else
			return 1;
	case UNARY_STRING:
		if (!strcmp(unary_expression->u.unary_expression.u.string, "true"))
			return 1;
		else if (!strcmp(unary_expression->u.unary_expression.u.string, "TRUE"))
			return 1;
		else if (!strcmp(unary_expression->u.unary_expression.u.string, "false"))
			return 0;
		else if (!strcmp(unary_expression->u.unary_expression.u.string, "FALSE"))
			return 0;
		else {
			fprintf(stderr, "[error] %s: unexpected string \"%s\"\n",
				__func__, unary_expression->u.unary_expression.u.string);
			return -EINVAL;
		}
		break;
	default:
		fprintf(stderr, "[error] %s: unexpected unary expression type\n",
			__func__);
		return -EINVAL;
	} 

}

static
int get_byte_order(FILE *fd, int depth, struct ctf_node *unary_expression,
		struct ctf_trace *trace)
{
	int byte_order;

	if (unary_expression->u.unary_expression.type != UNARY_STRING) {
		fprintf(stderr, "[error] %s: byte_order: expecting string\n",
			__func__);
		return -EINVAL;
	}
	if (!strcmp(unary_expression->u.unary_expression.u.string, "native"))
		byte_order = trace->byte_order;
	else if (!strcmp(unary_expression->u.unary_expression.u.string, "network"))
		byte_order = BIG_ENDIAN;
	else if (!strcmp(unary_expression->u.unary_expression.u.string, "be"))
		byte_order = BIG_ENDIAN;
	else if (!strcmp(unary_expression->u.unary_expression.u.string, "le"))
		byte_order = LITTLE_ENDIAN;
	else {
		fprintf(stderr, "[error] %s: unexpected string \"%s\". Should be \"native\", \"network\", \"be\" or \"le\".\n",
			__func__, unary_expression->u.unary_expression.u.string);
		return -EINVAL;
	}
	return byte_order;
}

static
struct declaration *ctf_declaration_integer_visit(FILE *fd, int depth,
		struct cds_list_head *expressions,
		struct ctf_trace *trace)
{
	struct ctf_node *expression;
	uint64_t alignment, size;
	int byte_order = trace->byte_order;
	int signedness = 0;
	int has_alignment = 0, has_size = 0;
	struct declaration_integer *integer_declaration;

	cds_list_for_each_entry(expression, expressions, siblings) {
		struct ctf_node *left, *right;

		left = _cds_list_first_entry(&expression->u.ctf_expression.left, struct ctf_node, siblings);
		right = _cds_list_first_entry(&expression->u.ctf_expression.right, struct ctf_node, siblings);
		assert(left->u.unary_expression.type == UNARY_STRING);
		if (!strcmp(left->u.unary_expression.u.string, "signed")) {
			signedness = get_boolean(fd, depth, right);
			if (signedness < 0)
				return NULL;
		} else if (!strcmp(left->u.unary_expression.u.string, "byte_order")) {
			byte_order = get_byte_order(fd, depth, right, trace);
			if (byte_order < 0)
				return NULL;
		} else if (!strcmp(left->u.unary_expression.u.string, "size")) {
			if (right->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT) {
				fprintf(stderr, "[error] %s: size: expecting unsigned constant\n",
					__func__);
				return NULL;
			}
			size = right->u.unary_expression.u.unsigned_constant;
			has_size = 1;
		} else if (!strcmp(left->u.unary_expression.u.string, "align")) {
			if (right->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT) {
				fprintf(stderr, "[error] %s: align: expecting unsigned constant\n",
					__func__);
				return NULL;
			}
			alignment = right->u.unary_expression.u.unsigned_constant;
			has_alignment = 1;
		} else {
			fprintf(stderr, "[error] %s: unknown attribute name %s\n",
				__func__, left->u.unary_expression.u.string);
			return NULL;
		}
	}
	if (!has_size) {
		fprintf(stderr, "[error] %s: missing size attribute\n", __func__);
		return NULL;
	}
	if (!has_alignment) {
		if (size % CHAR_BIT) {
			/* bit-packed alignment */
			alignment = 1;
		} else {
			/* byte-packed alignment */
			alignment = CHAR_BIT;
		}
	}
	integer_declaration = integer_declaration_new(size,
				byte_order, signedness, alignment);
	return &integer_declaration->p;
}

static
struct declaration *ctf_declaration_floating_point_visit(FILE *fd, int depth,
		struct cds_list_head *expressions,
		struct ctf_trace *trace)
{
	struct ctf_node *expression;
	uint64_t alignment, exp_dig, mant_dig, byte_order = trace->byte_order;
	int has_alignment = 0, has_exp_dig = 0, has_mant_dig = 0;
	struct declaration_float *float_declaration;

	cds_list_for_each_entry(expression, expressions, siblings) {
		struct ctf_node *left, *right;

		left = _cds_list_first_entry(&expression->u.ctf_expression.left, struct ctf_node, siblings);
		right = _cds_list_first_entry(&expression->u.ctf_expression.right, struct ctf_node, siblings);
		assert(left->u.unary_expression.type == UNARY_STRING);
		if (!strcmp(left->u.unary_expression.u.string, "byte_order")) {
			byte_order = get_byte_order(fd, depth, right, trace);
			if (byte_order < 0)
				return NULL;
		} else if (!strcmp(left->u.unary_expression.u.string, "exp_dig")) {
			if (right->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT) {
				fprintf(stderr, "[error] %s: exp_dig: expecting unsigned constant\n",
					__func__);
				return NULL;
			}
			exp_dig = right->u.unary_expression.u.unsigned_constant;
			has_exp_dig = 1;
		} else if (!strcmp(left->u.unary_expression.u.string, "mant_dig")) {
			if (right->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT) {
				fprintf(stderr, "[error] %s: mant_dig: expecting unsigned constant\n",
					__func__);
				return NULL;
			}
			mant_dig = right->u.unary_expression.u.unsigned_constant;
			has_mant_dig = 1;
		} else if (!strcmp(left->u.unary_expression.u.string, "align")) {
			if (right->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT) {
				fprintf(stderr, "[error] %s: align: expecting unsigned constant\n",
					__func__);
				return NULL;
			}
			alignment = right->u.unary_expression.u.unsigned_constant;
			has_alignment = 1;
		} else {
			fprintf(stderr, "[error] %s: unknown attribute name %s\n",
				__func__, left->u.unary_expression.u.string);
			return NULL;
		}
	}
	if (!has_mant_dig) {
		fprintf(stderr, "[error] %s: missing mant_dig attribute\n", __func__);
		return NULL;
	}
	if (!has_exp_dig) {
		fprintf(stderr, "[error] %s: missing exp_dig attribute\n", __func__);
		return NULL;
	}
	if (!has_alignment) {
		if ((mant_dig + exp_dig) % CHAR_BIT) {
			/* bit-packed alignment */
			alignment = 1;
		} else {
			/* byte-packed alignment */
			alignment = CHAR_BIT;
		}
	}
	float_declaration = float_declaration_new(mant_dig, exp_dig,
				byte_order, alignment);
	return &float_declaration->p;
}

static
struct declaration *ctf_declaration_string_visit(FILE *fd, int depth,
		struct cds_list_head *expressions,
		struct ctf_trace *trace)
{
	struct ctf_node *expression;
	const char *encoding_c = NULL;
	enum ctf_string_encoding encoding = CTF_STRING_UTF8;
	struct declaration_string *string_declaration;

	cds_list_for_each_entry(expression, expressions, siblings) {
		struct ctf_node *left, *right;

		left = _cds_list_first_entry(&expression->u.ctf_expression.left, struct ctf_node, siblings);
		right = _cds_list_first_entry(&expression->u.ctf_expression.right, struct ctf_node, siblings);
		assert(left->u.unary_expression.type == UNARY_STRING);
		if (!strcmp(left->u.unary_expression.u.string, "encoding")) {
			if (right->u.unary_expression.type != UNARY_STRING) {
				fprintf(stderr, "[error] %s: encoding: expecting string\n",
					__func__);
				return NULL;
			}
			encoding_c = right->u.unary_expression.u.string;
		} else {
			fprintf(stderr, "[error] %s: unknown attribute name %s\n",
				__func__, left->u.unary_expression.u.string);
			return NULL;
		}
	}
	if (encoding_c && !strcmp(encoding_c, "ASCII"))
		encoding = CTF_STRING_ASCII;
	string_declaration = string_declaration_new(encoding);
	return &string_declaration->p;
}


/*
 * Also add named variant, struct or enum to the current declaration scope.
 * FIXME: we are only taking the first one. check for root declaration specifiers list.
 */
static
struct declaration *ctf_declaration_specifier_visit(FILE *fd,
		int depth, struct cds_list_head *head,
		struct declaration_scope *declaration_scope,
		struct ctf_trace *trace)
{
	struct ctf_node *first;

	first = _cds_list_first_entry(head, struct ctf_node, siblings);

	switch (first->type) {
	case NODE_STRUCT:
		return ctf_declaration_struct_visit(fd, depth,
			first->u._struct.name,
			&first->u._struct.declaration_list,
			first->u._struct.has_body,
			declaration_scope,
			trace);
	case NODE_VARIANT:
		return ctf_declaration_variant_visit(fd, depth,
			first->u.variant.name,
			first->u.variant.choice,
			&first->u.variant.declaration_list,
			first->u.variant.has_body,
			declaration_scope,
			trace);
	case NODE_ENUM:
		return ctf_declaration_enum_visit(fd, depth,
			first->u._enum.enum_id,
			&first->u._enum.container_type,
			&first->u._enum.enumerator_list,
			first->u._enum.has_body,
			declaration_scope,
			trace);
	case NODE_INTEGER:
		return ctf_declaration_integer_visit(fd, depth,
			&first->u.integer.expressions, trace);
	case NODE_FLOATING_POINT:
		return ctf_declaration_floating_point_visit(fd, depth,
			&first->u.floating_point.expressions, trace);
	case NODE_STRING:
		return ctf_declaration_string_visit(fd, depth,
			&first->u.string.expressions, trace);
	case NODE_TYPE_SPECIFIER:
		return ctf_declaration_type_specifier_visit(fd, depth,
				head, declaration_scope);
	default:
		fprintf(stderr, "[error] %s: unexpected node type %d\n", __func__, (int) first->type);
		return NULL;
	}
}

static
int ctf_event_declaration_visit(FILE *fd, int depth, struct ctf_node *node, struct ctf_event *event, struct ctf_trace *trace)
{
	int ret = 0;

	switch (node->type) {
	case NODE_TYPEDEF:
		ret = ctf_typedef_visit(fd, depth + 1,
					event->declaration_scope,
					&node->u._typedef.declaration_specifier,
					&node->u._typedef.type_declarators,
					trace);
		if (ret)
			return ret;
		break;
	case NODE_TYPEALIAS:
		ret = ctf_typealias_visit(fd, depth + 1,
				event->declaration_scope,
				node->u.typealias.target, node->u.typealias.alias,
				trace);
		if (ret)
			return ret;
		break;
	case NODE_CTF_EXPRESSION:
	{
		char *left;

		left = concatenate_unary_strings(&node->u.ctf_expression.left);
		if (!strcmp(left, "name")) {
			char *right;

			if (CTF_EVENT_FIELD_IS_SET(event, name))
				return -EPERM;
			right = concatenate_unary_strings(&node->u.ctf_expression.right);
			if (!right) {
				fprintf(stderr, "[error] %s: unexpected unary expression for event name\n", __func__);
				return -EINVAL;
			}
			event->name = g_quark_from_string(right);
			g_free(right);
			CTF_EVENT_SET_FIELD(event, name);
		} else if (!strcmp(left, "id")) {
			if (CTF_EVENT_FIELD_IS_SET(event, id))
				return -EPERM;
			ret = get_unary_unsigned(&node->u.ctf_expression.right, &event->id);
			if (ret) {
				fprintf(stderr, "[error] %s: unexpected unary expression for event id\n", __func__);
				return -EINVAL;
			}
			CTF_EVENT_SET_FIELD(event, id);
		} else if (!strcmp(left, "stream_id")) {
			if (CTF_EVENT_FIELD_IS_SET(event, stream_id))
				return -EPERM;
			ret = get_unary_unsigned(&node->u.ctf_expression.right, &event->stream_id);
			if (ret) {
				fprintf(stderr, "[error] %s: unexpected unary expression for event stream_id\n", __func__);
				return -EINVAL;
			}
			event->stream = trace_stream_lookup(trace, event->stream_id);
			if (!event->stream) {
				fprintf(stderr, "[error] %s: stream id %" PRIu64 " cannot be found\n", __func__, event->stream_id);
				return -EINVAL;
			}
			CTF_EVENT_SET_FIELD(event, stream_id);
		} else if (!strcmp(left, "context")) {
			struct declaration *declaration;

			declaration = ctf_declaration_specifier_visit(fd, depth,
					&node->u.ctf_expression.right,
					event->declaration_scope, trace);
			if (!declaration)
				return -EPERM;
			if (declaration->id != CTF_TYPE_STRUCT)
				return -EPERM;
			event->context_decl = container_of(declaration, struct declaration_struct, p);
		} else if (!strcmp(left, "fields")) {
			struct declaration *declaration;

			declaration = ctf_declaration_specifier_visit(fd, depth,
					&node->u.ctf_expression.right,
					event->declaration_scope, trace);
			if (!declaration)
				return -EPERM;
			if (declaration->id != CTF_TYPE_STRUCT)
				return -EPERM;
			event->fields_decl = container_of(declaration, struct declaration_struct, p);
		}
		g_free(left);
		break;
	}
	default:
		return -EPERM;
	/* TODO: declaration specifier should be added. */
	}

	return 0;
}

static
int ctf_event_visit(FILE *fd, int depth, struct ctf_node *node,
		    struct declaration_scope *parent_declaration_scope, struct ctf_trace *trace)
{
	int ret = 0;
	struct ctf_node *iter;
	struct ctf_event *event;
	struct definition_scope *parent_def_scope;

	event = g_new0(struct ctf_event, 1);
	event->declaration_scope = new_declaration_scope(parent_declaration_scope);
	cds_list_for_each_entry(iter, &node->u.event.declaration_list, siblings) {
		ret = ctf_event_declaration_visit(fd, depth + 1, iter, event, trace);
		if (ret)
			goto error;
	}
	if (!CTF_EVENT_FIELD_IS_SET(event, name)) {
		ret = -EPERM;
		goto error;
	}
	if (!CTF_EVENT_FIELD_IS_SET(event, id)) {
		ret = -EPERM;
		goto error;
	}
	if (!CTF_EVENT_FIELD_IS_SET(event, stream_id)) {
		ret = -EPERM;
		goto error;
	}
	if (event->stream->events_by_id->len <= event->id)
		g_ptr_array_set_size(event->stream->events_by_id, event->id + 1);
	g_ptr_array_index(event->stream->events_by_id, event->id) = event;
	g_hash_table_insert(event->stream->event_quark_to_id,
			    (gpointer)(unsigned long) event->name,
			    &event->id);
	parent_def_scope = event->stream->definition_scope;
	if (event->context_decl) {
		event->context =
			container_of(
			event->context_decl->p.definition_new(&event->context_decl->p,
				parent_def_scope, 0, 0),
			struct definition_struct, p);
		set_dynamic_definition_scope(&event->context->p,
					     event->context->scope,
					     "event.context");
		parent_def_scope = event->context->scope;
		declaration_unref(&event->context_decl->p);
	}
	if (event->fields_decl) {
		event->fields =
			container_of(
			event->fields_decl->p.definition_new(&event->fields_decl->p,
				parent_def_scope, 0, 0),
			struct definition_struct, p);
		set_dynamic_definition_scope(&event->fields->p,
					     event->fields->scope,
					     "event.fields");
		parent_def_scope = event->fields->scope;
		declaration_unref(&event->fields_decl->p);
	}
	return 0;

error:
	declaration_unref(&event->fields_decl->p);
	declaration_unref(&event->context_decl->p);
	free_declaration_scope(event->declaration_scope);
	g_free(event);
	return ret;
}

 
static
int ctf_stream_declaration_visit(FILE *fd, int depth, struct ctf_node *node, struct ctf_stream *stream, struct ctf_trace *trace)
{
	int ret = 0;

	switch (node->type) {
	case NODE_TYPEDEF:
		ret = ctf_typedef_visit(fd, depth + 1,
					stream->declaration_scope,
					&node->u._typedef.declaration_specifier,
					&node->u._typedef.type_declarators,
					trace);
		if (ret)
			return ret;
		break;
	case NODE_TYPEALIAS:
		ret = ctf_typealias_visit(fd, depth + 1,
				stream->declaration_scope,
				node->u.typealias.target, node->u.typealias.alias,
				trace);
		if (ret)
			return ret;
		break;
	case NODE_CTF_EXPRESSION:
	{
		char *left;

		left = concatenate_unary_strings(&node->u.ctf_expression.left);
		if (!strcmp(left, "stream_id")) {
			if (CTF_STREAM_FIELD_IS_SET(stream, stream_id))
				return -EPERM;
			ret = get_unary_unsigned(&node->u.ctf_expression.right, &stream->stream_id);
			if (ret) {
				fprintf(stderr, "[error] %s: unexpected unary expression for event stream_id\n", __func__);
				return -EINVAL;
			}
			CTF_STREAM_SET_FIELD(stream, stream_id);
		} else if (!strcmp(left, "event.header")) {
			struct declaration *declaration;

			declaration = ctf_declaration_specifier_visit(fd, depth,
					&node->u.ctf_expression.right,
					stream->declaration_scope, trace);
			if (!declaration)
				return -EPERM;
			if (declaration->id != CTF_TYPE_STRUCT)
				return -EPERM;
			stream->event_header_decl = container_of(declaration, struct declaration_struct, p);
		} else if (!strcmp(left, "event.context")) {
			struct declaration *declaration;

			declaration = ctf_declaration_specifier_visit(fd, depth,
					&node->u.ctf_expression.right,
					stream->declaration_scope, trace);
			if (!declaration)
				return -EPERM;
			if (declaration->id != CTF_TYPE_STRUCT)
				return -EPERM;
			stream->event_context_decl = container_of(declaration, struct declaration_struct, p);
		} else if (!strcmp(left, "packet.context")) {
			struct declaration *declaration;

			declaration = ctf_declaration_specifier_visit(fd, depth,
					&node->u.ctf_expression.right,
					stream->declaration_scope, trace);
			if (!declaration)
				return -EPERM;
			if (declaration->id != CTF_TYPE_STRUCT)
				return -EPERM;
			stream->packet_context_decl = container_of(declaration, struct declaration_struct, p);
		}
		g_free(left);
		break;
	}
	default:
		return -EPERM;
	/* TODO: declaration specifier should be added. */
	}

	return 0;
}

static
int ctf_stream_visit(FILE *fd, int depth, struct ctf_node *node,
		     struct declaration_scope *parent_declaration_scope, struct ctf_trace *trace)
{
	int ret = 0;
	struct ctf_node *iter;
	struct ctf_stream *stream;
	struct definition_scope *parent_def_scope;

	stream = g_new0(struct ctf_stream, 1);
	stream->declaration_scope = new_declaration_scope(parent_declaration_scope);
	stream->events_by_id = g_ptr_array_new();
	stream->event_quark_to_id = g_hash_table_new(g_int_hash, g_int_equal);
	cds_list_for_each_entry(iter, &node->u.stream.declaration_list, siblings) {
		ret = ctf_stream_declaration_visit(fd, depth + 1, iter, stream, trace);
		if (ret)
			goto error;
	}
	if (!CTF_STREAM_FIELD_IS_SET(stream, stream_id)) {
		ret = -EPERM;
		goto error;
	}
	if (trace->streams->len <= stream->stream_id)
		g_ptr_array_set_size(trace->streams, stream->stream_id + 1);
	g_ptr_array_index(trace->streams, stream->stream_id) = stream;

	parent_def_scope = NULL;
	if (stream->packet_context_decl) {
		stream->packet_context =
			container_of(
			stream->packet_context_decl->p.definition_new(&stream->packet_context_decl->p,
				parent_def_scope, 0, 0),
			struct definition_struct, p);
		set_dynamic_definition_scope(&stream->packet_context->p,
					     stream->packet_context->scope,
					     "stream.packet.context");
		parent_def_scope = stream->packet_context->scope;
		declaration_unref(&stream->packet_context_decl->p);
	}
	if (stream->event_header_decl) {
		stream->event_header =
			container_of(
			stream->event_header_decl->p.definition_new(&stream->event_header_decl->p,
				parent_def_scope, 0, 0),
			struct definition_struct, p);
		set_dynamic_definition_scope(&stream->event_header->p,
					     stream->event_header->scope,
					     "stream.event.header");
		parent_def_scope = stream->event_header->scope;
		declaration_unref(&stream->event_header_decl->p);
	}
	if (stream->event_context_decl) {
		stream->event_context =
			container_of(
			stream->event_context_decl->p.definition_new(&stream->event_context_decl->p,
				parent_def_scope, 0, 0),
			struct definition_struct, p);
		set_dynamic_definition_scope(&stream->event_context->p,
					     stream->event_context->scope,
					     "stream.event.context");
		parent_def_scope = stream->event_context->scope;
		declaration_unref(&stream->event_context_decl->p);
	}
	stream->definition_scope = parent_def_scope;

	return 0;

error:
	declaration_unref(&stream->event_header_decl->p);
	declaration_unref(&stream->event_context_decl->p);
	declaration_unref(&stream->packet_context_decl->p);
	g_ptr_array_free(stream->events_by_id, TRUE);
	g_hash_table_destroy(stream->event_quark_to_id);
	free_declaration_scope(stream->declaration_scope);
	g_free(stream);
	return ret;
}

static
int ctf_trace_declaration_visit(FILE *fd, int depth, struct ctf_node *node, struct ctf_trace *trace)
{
	int ret = 0;

	switch (node->type) {
	case NODE_TYPEDEF:
		ret = ctf_typedef_visit(fd, depth + 1,
					trace->declaration_scope,
					&node->u._typedef.declaration_specifier,
					&node->u._typedef.type_declarators,
					trace);
		if (ret)
			return ret;
		break;
	case NODE_TYPEALIAS:
		ret = ctf_typealias_visit(fd, depth + 1,
				trace->declaration_scope,
				node->u.typealias.target, node->u.typealias.alias,
				trace);
		if (ret)
			return ret;
		break;
	case NODE_CTF_EXPRESSION:
	{
		char *left;

		left = concatenate_unary_strings(&node->u.ctf_expression.left);
		if (!strcmp(left, "major")) {
			if (CTF_TRACE_FIELD_IS_SET(trace, major))
				return -EPERM;
			ret = get_unary_unsigned(&node->u.ctf_expression.right, &trace->major);
			if (ret) {
				fprintf(stderr, "[error] %s: unexpected unary expression for trace major number\n", __func__);
				return -EINVAL;
			}
			CTF_TRACE_SET_FIELD(trace, major);
		} else if (!strcmp(left, "minor")) {
			if (CTF_TRACE_FIELD_IS_SET(trace, minor))
				return -EPERM;
			ret = get_unary_unsigned(&node->u.ctf_expression.right, &trace->minor);
			if (ret) {
				fprintf(stderr, "[error] %s: unexpected unary expression for trace minor number\n", __func__);
				return -EINVAL;
			}
			CTF_TRACE_SET_FIELD(trace, minor);
		} else if (!strcmp(left, "word_size")) {
			if (CTF_TRACE_FIELD_IS_SET(trace, word_size))
				return -EPERM;
			ret = get_unary_unsigned(&node->u.ctf_expression.right, &trace->word_size);
			if (ret) {
				fprintf(stderr, "[error] %s: unexpected unary expression for trace word_size\n", __func__);
				return -EINVAL;
			}
			CTF_TRACE_SET_FIELD(trace, word_size);
		} else if (!strcmp(left, "uuid")) {
			if (CTF_TRACE_FIELD_IS_SET(trace, uuid))
				return -EPERM;
			ret = get_unary_uuid(&node->u.ctf_expression.right, &trace->uuid);
			if (ret) {
				fprintf(stderr, "[error] %s: unexpected unary expression for trace uuid\n", __func__);
				return -EINVAL;
			}
			CTF_TRACE_SET_FIELD(trace, uuid);
		}
		g_free(left);
		break;
	}
	default:
		return -EPERM;
	/* TODO: declaration specifier should be added. */
	}

	return 0;
}

static
int ctf_trace_visit(FILE *fd, int depth, struct ctf_node *node, struct ctf_trace *trace)
{
	int ret = 0;
	struct ctf_node *iter;

	if (trace->declaration_scope)
		return -EEXIST;
	trace->declaration_scope = new_declaration_scope(trace->root_declaration_scope);
	trace->streams = g_ptr_array_new();
	cds_list_for_each_entry(iter, &node->u.trace.declaration_list, siblings) {
		ret = ctf_trace_declaration_visit(fd, depth + 1, iter, trace);
		if (ret)
			goto error;
	}
	if (!CTF_TRACE_FIELD_IS_SET(trace, major)) {
		ret = -EPERM;
		goto error;
	}
	if (!CTF_TRACE_FIELD_IS_SET(trace, minor)) {
		ret = -EPERM;
		goto error;
	}
	if (!CTF_TRACE_FIELD_IS_SET(trace, uuid)) {
		ret = -EPERM;
		goto error;
	}
	if (!CTF_TRACE_FIELD_IS_SET(trace, word_size)) {
		ret = -EPERM;
		goto error;
	}
	return 0;

error:
	g_ptr_array_free(trace->streams, TRUE);
	free_declaration_scope(trace->declaration_scope);
	return ret;
}

int ctf_visitor_construct_metadata(FILE *fd, int depth, struct ctf_node *node,
		struct ctf_trace *trace, int byte_order)
{
	int ret = 0;
	struct ctf_node *iter;

	trace->byte_order = byte_order;

	switch (node->type) {
	case NODE_ROOT:
		cds_list_for_each_entry(iter, &node->u.root.declaration_list,
					siblings) {
			switch (iter->type) {
			case NODE_TYPEDEF:
				ret = ctf_typedef_visit(fd, depth + 1,
							trace->root_declaration_scope,
							&iter->u._typedef.declaration_specifier,
							&iter->u._typedef.type_declarators,
							trace);
				if (ret)
					return ret;
				break;
			case NODE_TYPEALIAS:
				ret = ctf_typealias_visit(fd, depth + 1,
						trace->root_declaration_scope,
						iter->u.typealias.target, iter->u.typealias.alias,
						trace);
				if (ret)
					return ret;
				break;
			case NODE_DECLARATION_SPECIFIER:
				ret = ctf_declaration_specifier_visit(fd, depth, iter,
						trace->root_declaration_scope, trace);
				if (ret)
					return ret;
				break;
			default:
				fprintf(stderr, "[error] %s: unexpected root child type %d\n", __func__,
					(int) iter->type);
				return -EINVAL;
		}
		cds_list_for_each_entry(iter, &node->u.root.trace, siblings) {
			ret = ctf_trace_visit(fd, depth + 1, iter, trace);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.root.stream, siblings) {
			ret = ctf_stream_visit(fd, depth + 1, iter,
		    			       trace->root_declaration_scope, trace);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.root.event, siblings) {
			ret = ctf_event_visit(fd, depth + 1, iter,
		    			      trace->root_declaration_scope, trace);
			if (ret)
				return ret;
		}
		break;
	case NODE_UNKNOWN:
	default:
		fprintf(stderr, "[error] %s: unknown node type %d\n", __func__,
			(int) node->type);
		return -EINVAL;
	}
	return ret;
}