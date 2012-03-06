#ifndef _BABELTRACE_CTF_EVENTS_INTERNAL_H
#define _BABELTRACE_CTF_EVENTS_INTERNAL_H

/*
 * BabelTrace
 *
 * CTF events API (internal)
 *
 * Copyright 2011-2012 EfficiOS Inc. and Linux Foundation
 *
 * Author: Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *         Julien Desfossez <julien.desfossez@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 */

#include <babeltrace/iterator-internal.h>
#include <babeltrace/ctf/callbacks.h>
#include <babeltrace/ctf/callbacks-internal.h>
#include <glib.h>

struct bt_ctf_iter {
	struct bt_iter parent;
	struct bt_ctf_event current_ctf_event;	/* last read event */
	GArray *callbacks;				/* Array of struct bt_stream_callbacks */
	struct bt_callback_chain main_callbacks;	/* For all events */
	/*
	 * Flag indicating if dependency graph needs to be recalculated.
	 * Set by bt_iter_add_callback(), and checked (and
	 * cleared) by upon entry into bt_iter_read_event().
	 * bt_iter_read_event() is responsible for calling dep
	 * graph calculation if it sees this flag set.
	 */
	int recalculate_dep_graph;
	/*
	 * Array of pointers to struct bt_dependencies, for garbage
	 * collection. We're not using a linked list here because each
	 * struct bt_dependencies can belong to more than one
	 * bt_iter.
	 */
	GPtrArray *dep_gc;
};

#endif /*_BABELTRACE_CTF_EVENTS_INTERNAL_H */