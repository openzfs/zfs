#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libintl.h>

#include "libzfs.h"

stream_list_t *output_list = NULL;

static stream_node_t *stream_create_node(stream_t *output, stream_node_t *next);
static stream_node_t *stream_list_append_node(stream_t *output_entry,
    stream_list_t *stream_ll);

void
nomem_print(void)
{
	stream_print_list();
	(void) fprintf(stderr, gettext("internal error: out of memory\n"));
	exit(1);
}

/*
 * create_node:
 * Create a node for property list.
 * returns NULL if nomem
 */
static stream_node_t *
stream_create_node(stream_t *output, stream_node_t *next)
{
	stream_node_t *new_node = malloc(sizeof (stream_node_t));

	if (new_node == NULL) {
		return (NULL);
	}

	new_node->output = output;
	new_node->next = next;

	return (new_node);
}

/* Data structures */

void
stream_print_list()
{
	if (output_list != NULL) {
		FILE *fp;
		stream_node_t *current = output_list->head;

		while (current != NULL) {
			fp = current->output->err ? stderr : stdout;
			fflush(current->output->fd);
			fprintf(fp, "%s", *current->output->buf);
			current = current->next;
		}
	}
}

static stream_node_t *
stream_list_append_node(stream_t *output_entry, stream_list_t *stream_ll)
{
	stream_node_t *new_node = stream_create_node(output_entry, NULL);

	if (new_node == NULL) {
		nomem_print();
	}

	if (stream_ll->head == NULL) {
		stream_ll->head = new_node;
		stream_ll->tail = new_node;
		return (new_node);
	}

	if (stream_ll->tail != NULL) {
		stream_ll->tail->next = new_node;
		stream_ll->tail = new_node;
	}

	return (stream_ll->tail);
}


FILE *
set_stream(boolean_t err)
{
	stream_t *output;

	if ((output = malloc(sizeof (stream_t))) == NULL) {
		nomem_print();
	}


	if ((output->buf = malloc(sizeof (char *))) == NULL) {
		nomem_print();
	}


	if ((output->buf_len = malloc(sizeof (size_t))) == NULL) {
		nomem_print();
	}

	output->fd = open_memstream(output->buf, output->buf_len);
	output->err = err;

	(void) stream_list_append_node(output, output_list);

	return (output->fd);
}


void
free_stream(stream_t *output)
{
	if (output->buf != NULL) {
		free(output->buf);
	}

	free(output);
}

void
destroy_stream_list(stream_list_t *list)
{
	stream_node_t *head = list->head;
	stream_node_t *temp;

	while (head != NULL) {
		temp = head;
		head = head->next;
		free_stream(temp->output);
		free(temp);
	}
}
