/**
 * @file db_insert.c
 * Inserting a key-value pair into a DB hash
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "odb_hash.h"

int odb_insert(samples_odb_t * hash, odb_key_t key, odb_value_t value)
{
	odb_index_t index;
	odb_index_t new_node;
	odb_node_t * node;

	index = hash->hash_base[do_hash(hash, key)];
	while (index) {
		if (index <= 0 || index >= hash->descr->current_size) {
			char * err_msg;
			asprintf(&err_msg, "odb_insert() invalid index %u\n",
				 index);
			odb_set_error(hash, err_msg);
			return EXIT_FAILURE;
		}
		node = &hash->node_base[index];
		if (node->key == key) {
			if (node->value + value >= node->value) {
				node->value += value;
			} else {
				/* post profile tools must handle overflow */
				node->value = ~(odb_value_t)0;
			}
			return EXIT_SUCCESS;
		}

		index = node->next;
	}

	/* no locking is necessary: iteration interface retrieve data through
	 * the node_base array, odb_hash_add_node() increase current_size but
	 * odb_travel just ignore node with a zero key so on setting the key
	 * atomically update the node */
	new_node = odb_hash_add_node(hash);
	if (new_node == ODB_NODE_NR_INVALID) {
		return EXIT_FAILURE;
	}

	node = &hash->node_base[new_node];
	node->value = value;
	node->key = key;

	/* we need to recalculate hash code, hash table has perhaps grown */
	index = do_hash(hash, key);
	node->next = hash->hash_base[index];
	hash->hash_base[index] = new_node;
	
	return EXIT_SUCCESS;
}
