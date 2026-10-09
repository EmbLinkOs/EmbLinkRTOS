/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Intrusive circular doubly linked list with a sentinel head. Every operation is O(1)
 * except iteration. A node that is not in any list has next == NULL, which is how
 * the kernel tells an armed timeout node from a free one. No locking: the caller's
 * lock domain protects the list (CODING-STANDARD CS-2.6).
 */
#ifndef EMBK_LIST_H
#define EMBK_LIST_H

#include <emb/compiler.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct embk_list_node {
    struct embk_list_node *next;
    struct embk_list_node *prev;
} embk_list_node_t;

typedef struct embk_list {
    embk_list_node_t head; /* sentinel: head.next is the first node, head.prev the last */
} embk_list_t;

static EMB_INLINE void embk_list_init(embk_list_t *l)
{
    l->head.next = &l->head;
    l->head.prev = &l->head;
}

static EMB_INLINE void embk_list_node_init(embk_list_node_t *n)
{
    n->next = NULL;
    n->prev = NULL;
}

static EMB_INLINE bool embk_list_is_empty(const embk_list_t *l)
{
    return l->head.next == &l->head;
}

static EMB_INLINE bool embk_list_node_is_linked(const embk_list_node_t *n)
{
    return n->next != NULL;
}

static EMB_INLINE embk_list_node_t *embk_list_first(const embk_list_t *l)
{
    return embk_list_is_empty(l) ? NULL : l->head.next;
}

static EMB_INLINE embk_list_node_t *embk_list_last(const embk_list_t *l)
{
    return embk_list_is_empty(l) ? NULL : l->head.prev;
}

/* The node after @n, or NULL at the end of @l. */
static EMB_INLINE embk_list_node_t *embk_list_next(const embk_list_t *l, const embk_list_node_t *n)
{
    return (n->next == &l->head) ? NULL : n->next;
}

static EMB_INLINE embk_list_node_t *embk_list_prev(const embk_list_t *l, const embk_list_node_t *n)
{
    return (n->prev == &l->head) ? NULL : n->prev;
}

/* Insert @n after @pos (@pos may be the sentinel, giving insert-at-head). */
static EMB_INLINE void embk_list_insert_after(embk_list_node_t *pos, embk_list_node_t *n)
{
    n->prev = pos;
    n->next = pos->next;
    pos->next->prev = n;
    pos->next = n;
}

static EMB_INLINE void embk_list_insert_before(embk_list_node_t *pos, embk_list_node_t *n)
{
    n->next = pos;
    n->prev = pos->prev;
    pos->prev->next = n;
    pos->prev = n;
}

static EMB_INLINE void embk_list_append(embk_list_t *l, embk_list_node_t *n)
{
    embk_list_insert_before(&l->head, n);
}

static EMB_INLINE void embk_list_prepend(embk_list_t *l, embk_list_node_t *n)
{
    embk_list_insert_after(&l->head, n);
}

static EMB_INLINE void embk_list_remove(embk_list_node_t *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
    n->next = NULL;
    n->prev = NULL;
}

static EMB_INLINE embk_list_node_t *embk_list_pop_first(embk_list_t *l)
{
    embk_list_node_t *n = embk_list_first(l);
    if (n != NULL) {
        embk_list_remove(n);
    }
    return n;
}

#endif /* EMBK_LIST_H */
