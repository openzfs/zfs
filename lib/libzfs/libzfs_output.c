#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libintl.h>

#include "zfs_util.h"
#include "libzfs.h"

stream_list_t *output_list;
boolean_t use_stdout = B_TRUE;

static stream_node_t *stream_create_node(stream_t *output, stream_node_t *next);
static stream_node_t *stream_list_append_node(stream_t *output_entry,
    stream_list_t *stream_ll);

void
nomem_print(stream_list_t *stream_output_list)
{
	stream_print_list(stream_output_list);
	nomem();
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
stream_print_list_destroy(stream_list_t *stream_output_list)
{
	stream_node_t *temp;
	/* fprintf(stdout, "Stream\n"); */

	if (stream_output_list != NULL) {
		FILE *fp;
		stream_node_t *current = stream_output_list->head;

		while (current != NULL) {
			fp = current->output->err ? stderr : stdout;
			fclose(current->output->fd);
			fprintf(fp, "%s", *current->output->buf);

			temp = current;
			current = current->next;

			/* Clean up while printing */
			free_stream(temp->output);
			free(temp);
		}
	}
	free(stream_output_list);
	/* fprintf(stdout, "Stream END\n"); */
}

void
stream_print_list(stream_list_t *stream_output_list)
{
	if (stream_output_list != NULL) {
		FILE *fp;
		stream_node_t *current = stream_output_list->head;

		while (current != NULL) {
			fp = current->output->err ? stderr : stdout;
			fclose(current->output->fd);
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
		nomem_print(output_list);
	}

	if (stream_ll == NULL) {
		fprintf(stderr, "Stream uninitialized\n");
		exit(1);
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

int
init_stream_list(stream_list_t **stream_output_list)
{
	*stream_output_list = malloc(sizeof (stream_list_t));
	if (*stream_output_list == NULL) {
		nomem();
	}

	use_stdout = B_FALSE;

	return (0);
}

FILE *
set_stream(boolean_t err)
{
	if (use_stdout) {
		return (err ? stderr : stdout);
	}

	/*
	 * If err hasn't changed, continue using memstream,
	 * otherwise open new one
	 */
	if ((output_list != NULL) && (output_list->tail != NULL)) {
		if (output_list->tail->output->err == err) {
			return (output_list->tail->output->fd);
		}
	}

	stream_t *output;

	if ((output = malloc(sizeof (stream_t))) == NULL) {
		nomem_print(output_list);
	}

	if ((output->buf = malloc(sizeof (char *))) == NULL) {
		nomem_print(output_list);
	}


	if ((output->buf_len = malloc(sizeof (size_t))) == NULL) {
		nomem_print(output_list);
	}

	output->fd = open_memstream(output->buf, output->buf_len);
	if (output->fd == NULL) {
		nomem_print(output_list);
	}

	output->err = err;

	(void) stream_list_append_node(output, output_list);

	return (output->fd);
}

FILE *
stre(void)
{
	return (set_stream(B_TRUE));
}

FILE *
stro(void)
{
	return (set_stream(B_FALSE));
}

void
free_stream(stream_t *output)
{
	if (output->buf != NULL) {
		free(output->buf);
		free(output->buf_len);
	}

	free(output);
}

void
destroy_stream_list(stream_list_t *stream_output_list)
{
	stream_node_t *head = output_list->head;
	stream_node_t *temp;

	while (head != NULL) {
		temp = head;
		head = head->next;
		free_stream(temp->output);
		free(temp);
	}

	free(stream_output_list);
}
