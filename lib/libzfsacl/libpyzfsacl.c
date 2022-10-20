/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2022 Andrew Walker <awalker@ixsystems.com>
 * All rights reserved.
 */

#include <stdint.h>
#include <zfsacl.h>
#include <Python.h>

#define	Py_TPFLAGS_HAVE_ITER 0

typedef struct {
	PyObject_HEAD
	boolean_t verbose;
	zfsacl_t theacl;
} py_acl;

typedef struct {
	PyObject_HEAD
	py_acl *parent_acl;
	int idx;
	uint_t initial_cnt;
	zfsacl_entry_t theace;
} py_acl_entry;

typedef struct {
	PyObject_HEAD
	py_acl *acl;
	int current_idx;
} py_acl_iterator;

static PyObject *py_acl_inherit(PyObject *, PyObject *, PyObject *);

static void
set_exc_from_errno(const char *func)
{
	PyErr_Format(
	    PyExc_RuntimeError,
	    "%s failed: %s", func, strerror(errno));
}

static PyObject *
py_acl_iter_next(py_acl_iterator *self)
{
	PyObject *out = NULL;

	out = PyObject_CallMethod(
	    (PyObject *)self->acl, "get_entry", "i", self->current_idx);

	if (out == NULL) {
		if (PyErr_Occurred() == NULL) {
			return (NULL);
		}
		if (PyErr_ExceptionMatches(PyExc_IndexError)) {
			/* iteration done */
			PyErr_Clear();
			PyErr_SetNone(PyExc_StopIteration);
			return (NULL);
		}
		/* Some other error occurred */
		return (NULL);
	}

	self->current_idx++;
	return (out);
}

static void
py_acl_iter_dealloc(py_acl_iterator *self)
{
	Py_CLEAR(self->acl);
	PyObject_Del(self);
}

PyTypeObject PyACLIterator = {
	.tp_name = "ACL Iterator",
	.tp_basicsize = sizeof (py_acl_iterator),
	.tp_iternext = (iternextfunc)py_acl_iter_next,
	.tp_dealloc = (destructor)py_acl_iter_dealloc,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_iter = PyObject_SelfIter,
};

static inline PyObject *
aclflag_to_pylist(zfsacl_aclflags_t flags)
{
	int i, err;
	PyObject *out = NULL;

	out = Py_BuildValue("[]");
	if (out == NULL) {
		return (NULL);
	}

	for (i = 0; i < ARRAY_SIZE(aclflag2name); i++) {
		PyObject *val = NULL;

		if ((flags & aclflag2name[i].flag) == 0) {
			continue;
		}

		val = Py_BuildValue("s", aclflag2name[i].name);
		if (val == NULL) {
			Py_DECREF(out);
			return (NULL);
		}

		err = PyList_Append(out, val);
		Py_XDECREF(val);
		if (err == -1) {
			Py_XDECREF(out);
			return (NULL);
		}
	}

	return (out);
}

static PyObject *
permset_to_pylist(zfsace_permset_t perms)
{
	int i, err;
	PyObject *out = NULL;

	out = Py_BuildValue("[]");
	if (out == NULL) {
		return (NULL);
	}

	for (i = 0; i < ARRAY_SIZE(aceperm2name); i++) {
		PyObject *val = NULL;

		if ((perms & aceperm2name[i].perm) == 0) {
			continue;
		}

		val = Py_BuildValue("s", aceperm2name[i].name);
		if (val == NULL) {
			Py_DECREF(out);
			return (NULL);
		}

		err = PyList_Append(out, val);
		Py_XDECREF(val);
		if (err == -1) {
			Py_XDECREF(out);
			return (NULL);
		}
	}

	return (out);
}

static PyObject *
flagset_to_pylist(zfsace_flagset_t flags)
{
	int i, err;
	PyObject *out = NULL;
	out = Py_BuildValue("[]");
	if (out == NULL) {
		return (NULL);
	}

	for (i = 0; i < ARRAY_SIZE(aceflag2name); i++) {
		PyObject *val = NULL;

		if ((flags & aceflag2name[i].flag) == 0) {
			continue;
		}

		val = Py_BuildValue("s", aceflag2name[i].name);
		if (val == NULL) {
			Py_DECREF(out);
			return (NULL);
		}

		err = PyList_Append(out, val);
		Py_XDECREF(val);
		if (err == -1) {
			Py_XDECREF(out);
			return (NULL);
		}
	}

	return (out);
}

static PyObject *
whotype_to_pystring(zfsace_who_t whotype)
{
	int i;
	PyObject *out = NULL;

	for (i = 0; i < ARRAY_SIZE(acewho2name); i++) {
		if (whotype != acewho2name[i].who) {
			continue;
		}

		out = Py_BuildValue("s", acewho2name[i].name);
		if (out == NULL) {
			return (NULL);
		}
		return (out);
	}
	PyErr_Format(PyExc_ValueError, "%d is an invalid whotype", whotype);

	return (NULL);
}

static PyObject *
py_ace_new(PyTypeObject *obj, PyObject *args_unused,
    PyObject *kwargs_unused)
{
	py_acl_entry *self = NULL;

	self = (py_acl_entry *)obj->tp_alloc(obj, 0);
	if (self == NULL) {
		return (NULL);
	}
	self->theace = NULL;
	self->parent_acl = NULL;
	return ((PyObject *)self);
}

static int
py_ace_init(PyObject *obj, PyObject *args, PyObject *kwargs)
{
	return (0);
}

static void
py_ace_dealloc(py_acl_entry *self)
{
	if (self->parent_acl != NULL) {
		Py_CLEAR(self->parent_acl);
	}

	/*
	 * memory for ACL entry will be freed when
	 * ACL is deallocated.
	 */
	self->theace = NULL;
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
permset_to_basic(zfsace_permset_t perms)
{
	PyObject *out = NULL;

	if (perms == ZFSACE_FULL_SET) {
		out = Py_BuildValue("s", "FULL_CONTROL");
		return (out);
	} else if (perms == ZFSACE_MODIFY_SET) {
		out = Py_BuildValue("s", "MODIFY");
		return (out);
	} else if (perms == (ZFSACE_READ_SET | ZFSACE_EXECUTE)) {
		out = Py_BuildValue("s", "READ");
		return (out);
	} else if (perms == ZFSACE_TRAVERSE_SET) {
		out = Py_BuildValue("s", "TRAVERSE");
		return (out);
	}

	Py_RETURN_NONE;
}

static PyObject *
ace_get_permset(PyObject *obj, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;

	boolean_t ok;
	zfsace_permset_t perms;
	PyObject *out = NULL;

	ok = zfsace_get_permset(self->theace, &perms);
	if (!ok) {
		set_exc_from_errno("zfsace_get_permset()");
		return (NULL);
	}

	if (acl && acl->verbose) {
		PyObject *permlist = NULL;
		PyObject *basic = NULL;

		permlist = permset_to_pylist(perms);
		if (permlist == NULL) {
			return (NULL);
		}

		basic = permset_to_basic(perms);
		if (basic == NULL) {
			Py_XDECREF(permlist);
			return (NULL);
		}

		out = Py_BuildValue("{s:I,s:O,s:O}", "raw", perms,
		    "parsed", permlist, "basic", basic);

		Py_XDECREF(permlist);
		Py_XDECREF(basic);
	} else {
		out = Py_BuildValue("I", perms);
	}

	return (out);
}

static boolean_t
parse_permset(py_acl *acl, PyObject *to_parse,
    zfsace_permset_t *permset)
{
	unsigned long py_permset;

	if (!PyLong_Check(to_parse))
		return (B_FALSE);

	py_permset = PyLong_AsUnsignedLong(to_parse);

	if (py_permset == (unsigned long) -1)
		return (B_FALSE);

	if (ZFSACE_ACCESS_MASK_INVALID(py_permset)) {
		PyErr_SetString(PyExc_ValueError, "invalid flagset.");
		return (B_FALSE);
	}

	*permset = (zfsace_permset_t)py_permset;
	return (B_TRUE);
}

static int
ace_set_permset(PyObject *obj, PyObject *value, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;
	boolean_t ok;
	zfsace_permset_t permset;

	ok = parse_permset(acl, value, &permset);
	if (!ok) {
		return (-1);
	}

	ok = zfsace_set_permset(self->theace, permset);
	if (!ok) {
		set_exc_from_errno("zfsace_set_permset()");
		return (-1);
	}
	return (0);
}

static PyObject *
flagset_to_basic(zfsace_flagset_t flags)
{
	PyObject *out = NULL;

	/* inherited does not affect consideration of basic */
	flags &= ~ZFSACE_INHERITED_ACE;

	if (flags == (ZFSACE_DIRECTORY_INHERIT | ZFSACE_FILE_INHERIT)) {
		out = Py_BuildValue("s", "INHERIT");
		return (out);
	} else if (flags == 0) {
		out = Py_BuildValue("s", "NO_INHERIT");
		return (out);
	}

	Py_RETURN_NONE;
}

static PyObject *
ace_get_flagset(PyObject *obj, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;
	boolean_t ok;
	zfsace_flagset_t flags;
	PyObject *out = NULL;

	ok = zfsace_get_flagset(self->theace, &flags);
	if (!ok) {
		set_exc_from_errno("zfsace_get_flagset()");
		return (NULL);
	}

	if (acl && acl->verbose) {
		PyObject *flaglist = NULL;
		PyObject *basic = NULL;
		flaglist = flagset_to_pylist(flags);
		if (flaglist == NULL) {
			return (NULL);
		}

		basic = flagset_to_basic(flags);
		if (basic == NULL) {
			Py_XDECREF(flaglist);
			return (NULL);
		}

		out = Py_BuildValue("{s:I,s:O,s:O}", "raw", flags, "parsed",
		    flaglist, "basic", basic);

		Py_XDECREF(flaglist);
		Py_XDECREF(basic);
	} else {
		out = Py_BuildValue("I", flags);
	}

	return (out);
}

static boolean_t
parse_flagset(py_acl *acl, PyObject *to_parse,
    zfsace_flagset_t *flagset)
{
	unsigned long py_flagset;

	if (!PyLong_Check(to_parse))
		return (B_FALSE);

	py_flagset = PyLong_AsUnsignedLong(to_parse);

	if (py_flagset == (unsigned long) -1)
		return (B_FALSE);

	if (ZFSACE_FLAG_INVALID(py_flagset)) {
		PyErr_SetString(PyExc_ValueError, "invalid flagset.");
		return (B_FALSE);
	}

	*flagset = (zfsace_flagset_t)py_flagset;
	return (B_TRUE);
}

static int
ace_set_flagset(PyObject *obj, PyObject *value, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;
	boolean_t ok;
	zfsace_flagset_t flagset;

	ok = parse_flagset(acl, value, &flagset);
	if (!ok) {
		return (-1);
	}

	ok = zfsace_set_flagset(self->theace, flagset);
	if (!ok) {
		set_exc_from_errno("zfsace_set_flagset()");
		return (-1);
	}
	return (0);
}

static PyObject *
verbose_who(zfsace_who_t whotype, zfsace_id_t whoid)
{
	PyObject *pywhotype = NULL;
	PyObject *pywhoid = NULL;
	PyObject *verbose_whotype = NULL;
	PyObject *out = NULL;

	pywhotype = whotype_to_pystring(whotype);
	if (pywhotype == NULL) {
		return (NULL);
	}

	verbose_whotype = Py_BuildValue("{s:I,s:O}", "raw", whotype,
	    "parsed", pywhotype);

	Py_XDECREF(pywhotype);

	/*
	 * In future it may make sense to add getpwuid_r / getgrgid_r call here
	 */
	pywhoid = Py_BuildValue("{s:I,s:I}", "raw", whoid, "parsed", whoid);

	if (pywhoid == NULL) {
		Py_XDECREF(verbose_whotype);
		return (NULL);
	}

	out = Py_BuildValue("{s:O,s:O}", "who_type", verbose_whotype,
	    "who_id", pywhoid);

	Py_XDECREF(verbose_whotype);
	Py_XDECREF(pywhoid);
	return (out);
}

static PyObject *
ace_get_who(PyObject *obj, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;
	boolean_t ok;
	zfsace_who_t whotype;
	zfsace_id_t whoid;
	PyObject *out = NULL;

	ok = zfsace_get_who(self->theace, &whotype, &whoid);
	if (!ok) {
		set_exc_from_errno("zfsace_get_who()");
		return (NULL);
	}

	if (acl && acl->verbose) {
		out = verbose_who(whotype, whoid);
	} else {
		out = Py_BuildValue("II", whotype, whoid);
	}
	return (out);
}

static boolean_t
parse_who(py_acl *acl, PyObject *to_parse, zfsace_who_t *whotype,
    zfsace_id_t *whoid)
{
	int pywhotype, pywhoid;

	if (!PyArg_ParseTuple(to_parse, "ii", &pywhotype, &pywhoid))
		return (B_FALSE);

	if (SPECIAL_WHO_INVALID(pywhotype)) {
		PyErr_SetString(PyExc_ValueError, "invalid whotype.");
		return (B_FALSE);
	}

	if ((pywhoid < 0) && (pywhoid != -1)) {
		PyErr_SetString(PyExc_ValueError, "invalid id");
		return (B_FALSE);
	}

	if ((pywhoid == -1) &&
	    ((pywhotype == ZFSACL_USER) || (pywhotype == ZFSACL_USER))) {
		PyErr_SetString(PyExc_ValueError,
		    "-1 is invalid ID for named entries.");
		return (B_FALSE);
	}

	if (pywhoid > INT32_MAX) {
		PyErr_SetString(PyExc_ValueError,
		    "ID for named entry is too large.");
		return (B_FALSE);
	}

	*whotype = (zfsace_who_t)pywhotype;
	*whoid = (zfsace_id_t)pywhoid;

	return (B_TRUE);
}

static int
ace_set_who(PyObject *obj, PyObject *value, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;
	zfsace_who_t whotype;
	zfsace_id_t whoid;
	boolean_t ok;

	ok = parse_who(acl, value, &whotype, &whoid);
	if (!ok) {
		return (-1);
	}

	ok = zfsace_set_who(self->theace, whotype, whoid);
	if (!ok) {
		set_exc_from_errno("zfsace_set_who()");
		return (-1);
	}
	return (0);
}

static PyObject *
ace_get_entry_type(PyObject *obj, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;
	boolean_t ok;
	zfsace_entry_type_t entry_type;
	PyObject *out = NULL;

	ok = zfsace_get_entry_type(self->theace, &entry_type);
	if (!ok) {
		set_exc_from_errno("zfsace_get_entry_type()");
		return (NULL);
	}

	if (acl && acl->verbose) {
		const char *entry_str = NULL;

		switch (entry_type) {
		case ZFSACL_ENTRY_TYPE_ALLOW:
			entry_str = "ALLOW";
			break;
		case ZFSACL_ENTRY_TYPE_DENY:
			entry_str = "DENY";
			break;
		default:
			PyErr_Format(PyExc_ValueError,
			    "%d is an invalid entry type", entry_type);
			return (NULL);
		}
		out = Py_BuildValue("{s:I,s:s}", "raw", entry_type, "parsed",
		    entry_str);
	} else {
		out = Py_BuildValue("I", entry_type);
	}
	return (out);
}

static boolean_t
parse_entry_type(py_acl *acl, PyObject *to_parse,
    zfsace_entry_type_t *entry_type)
{
	unsigned long py_entry_type;


	if (!PyLong_Check(to_parse))
		return (B_FALSE);
	py_entry_type = PyLong_AsUnsignedLong(to_parse);

	if (py_entry_type == (unsigned long) -1)
		return (B_FALSE);

	if (ZFSACE_TYPE_INVALID(py_entry_type)) {
		PyErr_SetString(PyExc_ValueError, "invalid ACL entry type.");
		return (B_FALSE);
	}

	*entry_type = (zfsace_entry_type_t)py_entry_type;
	return (B_TRUE);
}

static int ace_set_entry_type(PyObject *obj, PyObject *value,
    void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	py_acl *acl = self->parent_acl;
	boolean_t ok;
	zfsace_entry_type_t entry_type;

	ok = parse_entry_type(acl, value, &entry_type);
	if (!ok) {
		return (-1);
	}

	ok = zfsace_set_entry_type(self->theace, entry_type);
	if (!ok) {
		set_exc_from_errno("zfsace_set_entry_type()");
		return (-1);
	}

	return (0);
}

static PyObject *
ace_get_idx(PyObject *obj, void *closure)
{
	py_acl_entry *self = (py_acl_entry *)obj;
	return (Py_BuildValue("i", self->idx));
}

static PyMethodDef ace_object_methods[] = {
	{ NULL, NULL, 0, NULL }
};

/* BEGIN CSTYLED */
PyDoc_STRVAR(py_ace_idx__doc__,
	"Position of Access control entry in the ACL.\n");

PyDoc_STRVAR(py_ace_permset__doc__,
"int : access mask for the access control list entry.\n"
"This should be bitwise or of following values as defined\n"
"in RFC 3530 Section 5.11.2.\n\n"
"Values\n"
"------\n"
"NFSv4 and POSIX1E common permissions:\n"
"zfsacl.PERM_READ_DATA - Permission to read data of the file\n"
"zfsacl.PERM_WRITE_DATA - Permission to modify file's data\n"
"zfsacl.PERM_EXECUTE - Permission to execute a file\n"
"NFSv4 brand specific permissions:\n"
"zfsacl.PERM_LIST_DIRECTORY - Permission to list contents of "
"a directory\n"
"zfsacl.PERM_ADD_FILE - Permission to add a new file to a directory\n"
"zfsacl.PERM_APPEND_DATA - Permission to append data to a file\n"
"zfsacl.PERM_ADD_SUBDIRECTORY - Permission to create a subdirectory "
"to a directory\n"
"zfsacl.PERM_READ_NAMED_ATTRS - Permission to read the named "
"attributes of a file\n"
"zfsacl.PERM_WRITE_NAMED_ATTRS - Permission to write the named "
"attributes of a file\n"
"zfsacl.PERM_DELETE_CHILD - Permission to delete a file or directory "
"within a directorey\n"
"zfsacl.PERM_READ_ATTRIBUTES - Permission to stat() a file\n"
"zfsacl.PERM_WRITE_ATTRIBUTES - Permission to change basic attributes\n"
"zfsacl.PERM_DELETE - Permission to delete the file\n"
"zfsacl.PERM_WRITE_ACL - Permission to write the ACL\n"
"zfsacl.PERM_WRITE_OWNER - Permission to change the owner\n"
"zfsacl.PERM_SYNCHRONIZE - Not Implemented\n\n"
"Warning\n"
"-------\n"
"The exact behavior of these permissions bits may vary depending\n"
"on operating system implementation. Please review relevant OS\n"
"documention and validate the behavior before deploying an access\n"
"control scheme in a production environment.\n"
);

PyDoc_STRVAR(py_ace_flagset__doc__,
"int : inheritance flags for the access control list entry.\n"
"This should be bitwise or of the following values as defined\n"
"in RFC 5661 Section 6.2.1.4.\n\n"
"Values\n"
"------\n"
"zfsacl.FLAG_FILE_INHERIT - Any non-directory file in any subdirectory\n"
"will get this ACE inherited\n"
"zfsacl.FLAG_DIRECTORY_INHERIT - This ACE will be added to any new "
"subdirectory created in this directory\n"
"zfsacl.FLAG_NO_PROPAGATE_INHERIT - Inheritance of this ACE should stop\n"
"at newly created child directories\n"
"zfsacl.FLAG_INHERIT_ONLY - ACE is not enforced on this directory, but\n"
"will be enforced (cleared) on newly created files and directories\n"
"zfsacl.FLAG_INHERITED - This ace was inherited from a parent directory\n\n"
"Note: flags are not valid for POSIX1E ACLs. The only flag valid for\n"
"files is zfsacl.FLAG_INHERITED, presence of other flags in any ACL entries\n"
"in an ACL will cause setacl attempt on a non-directory file to fail.\n"
);

PyDoc_STRVAR(py_ace_who__doc__,
"tuple : tuple containing information about to whom the ACL entry applies.\n"
"(<who_type>, <who_id>).\n\n"
"Values - whotype\n"
"----------------\n"
"zfsacl.WHOTYPE_USER_OBJ - The owning user of the file. If this is set, then numeric\n"
"id must be set to -1\n"
"zfsacl.WHOTYPE_GROUP_OBJ - The owning group of the file. If this is set, then\n"
"numeric id must be set to -1\n"
"zfsacl.WHOTYPE_EVERYONE - All users. For NFSv4 ACL brand, this includes the\n"
"file owner and group (as opposed to `other` in conventional POSIX mode)\n"
"zfsacl.WHOTYPE_USER - The numeric ID <who_id> is a user.\n"
"zfsacl.WHOTYPE_GROUP - The numeric ID <who_id> is a group.\n"
);

PyDoc_STRVAR(py_ace_entry_type__doc__,
"int : ACE type. See RFC 5661 Section 6.2.1.1 and relevant operating system\n"
"documentation for more implementation details.\n\n"
"Values\n"
"------\n"
"zfsacl.ENTRY_TYPE_ALLOW - Explicitly grants the access defined in permset\n"
"zfsacl.ENTRY_TYPE_DENY - Explicitly denies the access defined in permset\n"
);
/* END CSTYLED */

static PyGetSetDef ace_object_getsetters[] = {
	{
		.name	= "idx",
		.get	= (getter)ace_get_idx,
		.doc	= py_ace_idx__doc__,
	},
	{
		.name	= "permset",
		.get	= (getter)ace_get_permset,
		.set	= (setter)ace_set_permset,
		.doc	= py_ace_permset__doc__,
	},
	{
		.name	= "flagset",
		.get	= (getter)ace_get_flagset,
		.set	= (setter)ace_set_flagset,
		.doc	= py_ace_flagset__doc__,
	},
	{
		.name	= "who",
		.get	= (getter)ace_get_who,
		.set	= (setter)ace_set_who,
		.doc	= py_ace_who__doc__,
	},
	{
		.name	= "entry_type",
		.get	= (getter)ace_get_entry_type,
		.set	= (setter)ace_set_entry_type,
		.doc	= py_ace_entry_type__doc__,
	},
	{ .name = NULL }
};

static PyTypeObject PyZfsACLEntry = {
	.tp_name = "zfsacl.ACLEntry",
	.tp_basicsize = sizeof (py_acl_entry),
	.tp_methods = ace_object_methods,
	.tp_getset = ace_object_getsetters,
	.tp_new = py_ace_new,
	.tp_init = py_ace_init,
	.tp_doc = "An ACL Entry",
	.tp_dealloc = (destructor)py_ace_dealloc,
	.tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
};

static PyObject *
py_acl_new(PyTypeObject *obj, PyObject *args_unused,
    PyObject *kwargs_unused)
{
	py_acl *self = NULL;

	self = (py_acl *)obj->tp_alloc(obj, 0);
	if (self == NULL) {
		return (NULL);
	}
	self->theacl = NULL;
	self->verbose = B_FALSE;
	return ((PyObject *)self);
}

static int
py_acl_init(PyObject *obj, PyObject *args, PyObject *kwargs)
{
	py_acl *self = (py_acl *)obj;
	zfsacl_t theacl = NULL;
	char *kwnames[] = { "fd", "path", "brand", NULL };
	int fd = 0, brand = ZFSACL_BRAND_NFSV4;
	char *path = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|isi", kwnames,
	    &fd, &path, &brand)) {
		return (-1);
	}

	if (fd != 0) {
		theacl = zfsacl_get_fd(fd, brand);
		if (theacl == NULL) {
			set_exc_from_errno("zfsacl_get_fd()");
			return (-1);
		}
	} else if (path != NULL) {
		theacl = zfsacl_get_file(path, brand);
		if (theacl == NULL) {
			set_exc_from_errno("zfsacl_get_file()");
			return (-1);
		}
	} else {
		theacl = zfsacl_init(ZFSACL_MAX_ENTRIES, brand);
		if (theacl == NULL) {
			set_exc_from_errno("zfsacl_get_file()");
			return (-1);
		}
	}

	if (theacl == NULL) {
		set_exc_from_errno("zfsace_set_entry_type()");
		return (-1);
	}

	self->theacl = theacl;

	return (0);
}

static void
py_acl_dealloc(py_acl *self)
{
	if (self->theacl != NULL) {
		zfsacl_free(&self->theacl);
		self->theacl = NULL;
	}
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
acl_get_verbose(PyObject *obj, void *closure)
{
	py_acl *self = (py_acl *)obj;

	if (self->verbose) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

static int
acl_set_verbose(PyObject *obj, PyObject *value, void *closure)
{
	py_acl *self = (py_acl *)obj;

	if (!PyBool_Check(value)) {
		PyErr_SetString(PyExc_TypeError, "value must be boolean.");
		return (-1);
	}

	self->verbose = (value == Py_True) ? B_TRUE : B_FALSE;
	return (0);
}

static PyObject *
acl_get_flags(PyObject *obj, void *closure)
{
	py_acl *self = (py_acl *)obj;
	boolean_t ok;
	zfsacl_aclflags_t flags;
	PyObject *out = NULL;

	ok = zfsacl_get_aclflags(self->theacl, &flags);
	if (!ok) {
		set_exc_from_errno("zfsacl_get_aclflags()");
		return (NULL);
	}

	out = Py_BuildValue("I", flags);
	return (out);
}

static int
acl_set_flags(PyObject *obj, PyObject *value, void *closure)
{
	py_acl *self = (py_acl *)obj;
	long val;
	boolean_t ok;

	if (!PyLong_Check(value)) {
		PyErr_SetString(PyExc_TypeError, "flags must be integer");
		return (-1);
	}

	val = PyLong_AsLong(value);

	if (ZFSACL_FLAGS_INVALID(val)) {
		PyErr_SetString(PyExc_ValueError,
		    "Invalid ACL flags specified");
		return (-1);
	}

	ok = zfsacl_set_aclflags(self->theacl, (zfsacl_aclflags_t)val);
	if (!ok) {
		set_exc_from_errno("zfsacl_set_aclflags()");
		return (-1);
	}

	return (0);
}

static PyObject *
acl_get_brand(PyObject *obj, void *closure)
{
	py_acl *self = (py_acl *)obj;
	boolean_t ok;
	zfsacl_brand_t brand;
	PyObject *out = NULL;

	ok = zfsacl_get_brand(self->theacl, &brand);
	if (!ok) {
		set_exc_from_errno("zfsacl_get_brand()");
		return (NULL);
	}

	out = Py_BuildValue("I", brand);
	return (out);
}

static PyObject *
acl_get_acecnt(PyObject *obj, void *closure)
{
	py_acl *self = (py_acl *)obj;
	boolean_t ok;
	uint_t acecnt;
	PyObject *out = NULL;

	ok = zfsacl_get_acecnt(self->theacl, &acecnt);
	if (!ok) {
		set_exc_from_errno("zfsacl_get_acecnt()");
		return (NULL);
	}

	out = Py_BuildValue("I", acecnt);
	return (out);
}

static boolean_t
initialize_py_ace(py_acl *self, PyObject *in, int idx,
    zfsacl_entry_t entry)
{
	py_acl_entry *out = (py_acl_entry *)in;
	boolean_t ok;
	uint_t acecnt;

	ok = zfsacl_get_acecnt(self->theacl, &acecnt);
	if (!ok) {
		set_exc_from_errno("zfsacl_get_acecnt()");
		return (B_FALSE);
	}

	out->theace = entry;
	out->parent_acl = self;
	out->initial_cnt = acecnt;
	out->idx = (idx == ZFSACL_APPEND_ENTRY) ? (int)acecnt : idx;
	Py_INCREF(out->parent_acl);
	return (B_TRUE);
}

static boolean_t
pyargs_get_index(py_acl *self, PyObject *args, int *pidx,
    boolean_t required)
{
	int val = -1;
	boolean_t ok;
	uint_t acecnt;
	const char *format = required ? "i" : "|i";

	if (!PyArg_ParseTuple(args, format, &val))
		return (B_FALSE);

	if (val == -1) {
		*pidx = ZFSACL_APPEND_ENTRY;
		return (B_TRUE);
	} else if (val == 0) {
		*pidx = 0;
		return (B_TRUE);
	}

	if (val < 0) {
		PyErr_SetString(PyExc_ValueError, "Index may not be negative");
		return (B_FALSE);
	}

	if (val > (ZFSACL_MAX_ENTRIES -1)) {
		PyErr_SetString(PyExc_ValueError,
		    "Index exceeds maximum entries for ACL");
		return (B_FALSE);
	}

	ok = zfsacl_get_acecnt(self->theacl, &acecnt);
	if (!ok) {
		set_exc_from_errno("zfsacl_get_acecnt()");
		return (B_FALSE);
	}

	if ((acecnt == 0) || (((uint_t)val) > acecnt -1)) {
		PyErr_Format(PyExc_IndexError,
		    "%ld: index invalid, ACL contains (%u) entries.", val,
		    acecnt);
		return (B_FALSE);
	}

	if (val > (ZFSACL_MAX_ENTRIES -1)) {
		PyErr_SetString(PyExc_ValueError,
		    "Index exceeds maximum entries for ACL");
		return (B_FALSE);
	}

	*pidx = val;
	return (B_TRUE);
}

/* BEGIN CSTYLED */
PyDoc_STRVAR(py_acl_create_entry__doc__,
"create_entry(index)\n"
"--\n\n"
"Create a new ACL entry. If index is unspecified then entry\n"
"will be appended to ACL.\n\n"
"Parameters\n"
"----------\n"
"index : int, optional\n"
"    Position of new entry in ACL.\n\n"
"Returns\n"
"-------\n"
"    new zfsacl.ACLEntry object\n"
);
/* END CSTYLED */

static PyObject *
py_acl_create_entry(PyObject *obj, PyObject *args)
{
	py_acl *self = (py_acl *)obj;
	boolean_t ok;
	int idx;
	zfsacl_entry_t entry = NULL;
	PyObject *pyentry = NULL;

	ok = pyargs_get_index(self, args, &idx, B_FALSE);
	if (!ok) {
		return (NULL);
	}

	ok = zfsacl_create_aclentry(self->theacl, idx, &entry);
	if (!ok) {
		set_exc_from_errno("zfsacl_create_aclentry()");
		return (NULL);
	}

	pyentry = PyObject_CallFunction((PyObject *)&PyZfsACLEntry, NULL);
	ok = initialize_py_ace(self, pyentry, idx, entry);
	if (!ok) {
		Py_CLEAR(pyentry);
		return (NULL);
	}

	return ((PyObject *)pyentry);
}

/* BEGIN CSTYLED */
PyDoc_STRVAR(py_acl_get_entry__doc__,
"get_entry(index)\n"
"--\n\n"
"Retrieve ACL entry with specified index from ACL.\n\n"
"Parameters\n"
"----------\n"
"index : int\n"
"    Position of entry in ACL to be retrieved.\n\n"
"Returns\n"
"-------\n"
"    new zfsacl.ACLEntry object\n"
);
/* END CSTYLED */

static PyObject *
py_acl_get_entry(PyObject *obj, PyObject *args)
{
	py_acl *self = (py_acl *)obj;
	boolean_t ok;
	int idx;
	zfsacl_entry_t entry = NULL;
	PyObject *pyentry = NULL;

	ok = pyargs_get_index(self, args, &idx, B_TRUE);
	if (!ok) {
		return (NULL);
	}

	ok = zfsacl_get_aclentry(self->theacl, idx, &entry);
	if (!ok) {
		set_exc_from_errno("zfsacl_get_aclentry()");
		return (NULL);
	}

	pyentry = PyObject_CallFunction((PyObject *)&PyZfsACLEntry, NULL);
	ok = initialize_py_ace(self, pyentry, idx, entry);
	if (!ok) {
		Py_CLEAR(pyentry);
		return (NULL);
	}

	return ((PyObject *)pyentry);
}

/* BEGIN CSTYLED */
PyDoc_STRVAR(py_acl_delete_entry__doc__,
"delete_entry(index)\n"
"--\n\n"
"Remove the ACL entry specified by index from the ACL.\n\n"
"Parameters\n"
"----------\n"
"index : int\n"
"    Position of entry in ACL to be removed.\n\n"
"Returns\n"
"-------\n"
"    None\n"
);
/* END CSTYLED */

static PyObject *
py_acl_delete_entry(PyObject *obj, PyObject *args)
{
	py_acl *self = (py_acl *)obj;
	boolean_t ok;
	int idx;

	ok = pyargs_get_index(self, args, &idx, B_TRUE);
	if (!ok) {
		return (NULL);
	}

	ok = zfsacl_delete_aclentry(self->theacl, idx);
	if (!ok) {
		if ((errno == ERANGE) && (idx == 0)) {
			PyErr_SetString(PyExc_ValueError,
			    "At least one ACL entry is required.");
			return (NULL);
		}
		set_exc_from_errno("zfsacl_delete_aclentry()");
		return (NULL);
	}

	Py_RETURN_NONE;
}

/* BEGIN CSTYLED */
PyDoc_STRVAR(py_acl_set__doc__,
"setacl(fd=-1, path=None)\n"
"--\n\n"
"Set the acl on either a path or open file.\n"
"Either a path or file must be specified (not both).\n\n"
"Parameters\n"
"----------\n"
"fd : int, optional\n"
"    Open file descriptor to use for setting ACL.\n"
"path : string, optional\n"
"    Path of file on which to set ACL.\n\n"
"Returns\n"
"-------\n"
"    None\n"
);
/* END CSTYLED */

static PyObject *
py_acl_set(PyObject *obj, PyObject *args, PyObject *kwargs)
{
	py_acl *self = (py_acl *)obj;
	boolean_t ok;
	int fd = -1;
	const char *path = NULL;
	char *kwnames [] = { "fd", "path", NULL };

	ok = PyArg_ParseTupleAndKeywords(args, kwargs, "|is", kwnames, &fd,
	    &path);

	if (!ok) {
		return (NULL);
	}

	if (fd != -1) {
		ok = zfsacl_set_fd(fd, self->theacl);
		if (!ok) {
			set_exc_from_errno("zfsacl_set_fd()");
			return (NULL);
		}
	} else if (path != NULL) {
		ok = zfsacl_set_file(path, self->theacl);
		if (!ok) {
			set_exc_from_errno("zfsacl_set_file()");
			return (NULL);
		}
	} else {
		PyErr_SetString(PyExc_ValueError,
		    "`fd` or `path` key is required");
	}

	Py_RETURN_NONE;
}

static PyObject *
py_acl_iter(PyObject *obj, PyObject *args_unused)
{
	py_acl *self = (py_acl *)obj;
	py_acl_iterator *out = NULL;

	out = PyObject_New(py_acl_iterator, &PyACLIterator);
	if (out == NULL) {
		return (NULL);
	}

	out->current_idx = 0;
	out->acl = self;
	Py_INCREF(self);
	return ((PyObject *)out);
}

static PyMethodDef acl_object_methods[] = {
	{
		.ml_name = "setacl",
		.ml_meth = (PyCFunction)py_acl_set,
		.ml_flags = METH_VARARGS|METH_KEYWORDS,
		.ml_doc = py_acl_set__doc__
	},
	{
		.ml_name = "create_entry",
		.ml_meth = py_acl_create_entry,
		.ml_flags = METH_VARARGS,
		.ml_doc = py_acl_create_entry__doc__
	},
	{
		.ml_name = "get_entry",
		.ml_meth = py_acl_get_entry,
		.ml_flags = METH_VARARGS,
		.ml_doc = py_acl_get_entry__doc__
	},
	{
		.ml_name = "delete_entry",
		.ml_meth = py_acl_delete_entry,
		.ml_flags = METH_VARARGS,
		.ml_doc = py_acl_delete_entry__doc__
	},
	{
		.ml_name = "calculate_inherited_acl",
		.ml_meth = (PyCFunction)py_acl_inherit,
		.ml_flags = METH_VARARGS|METH_KEYWORDS,
		.ml_doc = "calculate an inherited ACL"
	},
	{ NULL, NULL, 0, NULL }
};

/* BEGIN CSTYLED */
PyDoc_STRVAR(py_acl_verbose__doc__,
"bool : Attribute controls whether information about the ACL\n"
"will be printed in verbose format.\n"
);

PyDoc_STRVAR(py_acl_flags__doc__,
"int : ACL-wide flags. For description of flags see RFC-5661\n"
"section 6.4.2.3 - Automatic Inheritance.\n\n"
"These flags are interpreted by client applications (for example \n"
"Samba) and should be evaluated by applications that recursively\n"
"manage ACLs.\n\n"
"Examples: zfsacl.AUTO_INHERIT, zfsacl.PROTECTED\n"
);

PyDoc_STRVAR(py_acl_brand__doc__,
"read-only attribute indicating the brand of ACL (POSIX1E or NFSv4).\n"
);

PyDoc_STRVAR(py_acl_ace_count__doc__,
"read-only attribute indicating the number of ACEs in the ACL.\n"
);
/* END CSTYLED */

static PyGetSetDef acl_object_getsetters[] = {
	{
		.name	= "verbose_output",
		.get	= (getter)acl_get_verbose,
		.set	= (setter)acl_set_verbose,
		.doc	= py_acl_verbose__doc__,
	},
	{
		.name	= "acl_flags",
		.get	= (getter)acl_get_flags,
		.set	= (setter)acl_set_flags,
		.doc	= py_acl_flags__doc__,
	},
	{
		.name	= "brand",
		.get	= (getter)acl_get_brand,
		.doc	= py_acl_brand__doc__,
	},
	{
		.name	= "ace_count",
		.get	= (getter)acl_get_acecnt,
		.doc	= py_acl_ace_count__doc__,
	},
	{ .name = NULL }
};

static PyTypeObject PyZfsACL = {
	.tp_name = "zfsacl.ACL",
	.tp_basicsize = sizeof (py_acl),
	.tp_methods = acl_object_methods,
	.tp_getset = acl_object_getsetters,
	.tp_new = py_acl_new,
	.tp_init = py_acl_init,
	.tp_doc = "An ACL",
	.tp_dealloc = (destructor)py_acl_dealloc,
	.tp_iter = (getiterfunc)py_acl_iter,
	.tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_ITER,
};

static PyObject *
py_acl_inherit(PyObject *obj, PyObject *args, PyObject *kwargs)
{
	py_acl *self = (py_acl *)obj, *out = NULL;
	boolean_t ok, isdir = B_TRUE;
	zfsacl_t parent, result;

	char *kwnames [] = { "is_dir", NULL };

	ok = PyArg_ParseTupleAndKeywords(args, kwargs, "|bO", kwnames,
	    &isdir);

	if (!ok) {
		return (NULL);
	}

	parent = self->theacl;

	result = zfsacl_calculate_inherited_acl(parent, NULL, isdir);
	if (result == NULL) {
		return (NULL);
	}

	out = (py_acl *)PyObject_CallFunction((PyObject *)&PyZfsACL, NULL);
	if (out == NULL) {
		return (NULL);
	}

	out->theacl = result;
	return ((PyObject *)out);
}

static PyMethodDef acl_module_methods[] = {
	{ .ml_name = NULL }
};
#define	MODULE_DOC "ZFS ACL python bindings."

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	.m_name = "zfsacl",
	.m_doc = MODULE_DOC,
	.m_size = -1,
	.m_methods = acl_module_methods,
};

PyObject*
module_init(void)
{
	PyObject *m = NULL;
	m = PyModule_Create(&moduledef);
	if (m == NULL) {
		fprintf(stderr, "failed to initalize module\n");
		return (NULL);
	}

	if (PyType_Ready(&PyZfsACL) < 0)
		return (NULL);

	if (PyType_Ready(&PyZfsACLEntry) < 0)
		return (NULL);

	/* ZFS ACL branding */
	PyModule_AddIntConstant(m, "BRAND_UNKNOWN", ZFSACL_BRAND_UNKNOWN);
	PyModule_AddIntConstant(m, "BRAND_ACCESS", ZFSACL_BRAND_ACCESS);
	PyModule_AddIntConstant(m, "BRAND_DEFAULT", ZFSACL_BRAND_DEFAULT);
	PyModule_AddIntConstant(m, "BRAND_NFSV4", ZFSACL_BRAND_NFSV4);

	/* ZFS ACL whotypes */
	PyModule_AddIntConstant(m, "WHOTYPE_UNDEFINED", ZFSACL_UNDEFINED_TAG);
	PyModule_AddIntConstant(m, "WHOTYPE_USER_OBJ", ZFSACL_USER_OBJ);
	PyModule_AddIntConstant(m, "WHOTYPE_GROUP_OBJ", ZFSACL_GROUP_OBJ);
	PyModule_AddIntConstant(m, "WHOTYPE_EVERYONE", ZFSACL_EVERYONE);
	PyModule_AddIntConstant(m, "WHOTYPE_USER", ZFSACL_USER);
	PyModule_AddIntConstant(m, "WHOTYPE_GROUP", ZFSACL_GROUP);
	PyModule_AddIntConstant(m, "WHOTYPE_MASK", ZFSACL_MASK);

	/* ZFS ACL entry types */
	PyModule_AddIntConstant(m, "ENTRY_TYPE_ALLOW", ZFSACL_ENTRY_TYPE_ALLOW);
	PyModule_AddIntConstant(m, "ENTRY_TYPE_DENY", ZFSACL_ENTRY_TYPE_DENY);

	/* ZFS ACL ACL-wide flags */
	PyModule_AddIntConstant(m, "ACL_AUTO_INHERIT", ZFSACL_AUTO_INHERIT);
	PyModule_AddIntConstant(m, "ACL_PROTECTED", ZFSACL_PROTECTED);
	PyModule_AddIntConstant(m, "ACL_DEFAULT", ZFSACL_DEFAULTED);

	/* valid on get, but not set */
	PyModule_AddIntConstant(m, "ACL_IS_TRIVIAL", ZFSACL_IS_TRIVIAL);

	/* ZFS ACL inherit flags (NFSv4 only) */
	PyModule_AddIntConstant(m, "FLAG_FILE_INHERIT", ZFSACE_FILE_INHERIT);
	PyModule_AddIntConstant(m, "FLAG_DIRECTORY_INHERIT",
	    ZFSACE_DIRECTORY_INHERIT);
	PyModule_AddIntConstant(m, "FLAG_NO_PROPAGATE_INHERIT",
	    ZFSACE_NO_PROPAGATE_INHERIT);
	PyModule_AddIntConstant(m, "FLAG_INHERIT_ONLY", ZFSACE_INHERIT_ONLY);
	PyModule_AddIntConstant(m, "FLAG_INHERITED", ZFSACE_INHERITED_ACE);

	/* ZFS ACL permissions */
	/* POSIX1e and NFSv4 */
	PyModule_AddIntConstant(m, "PERM_READ_DATA", ZFSACE_READ_DATA);
	PyModule_AddIntConstant(m, "PERM_WRITE_DATA", ZFSACE_WRITE_DATA);
	PyModule_AddIntConstant(m, "PERM_EXECUTE", ZFSACE_EXECUTE);

	/* NFSv4 only */
	PyModule_AddIntConstant(m, "PERM_LIST_DIRECTORY",
	    ZFSACE_LIST_DIRECTORY);
	PyModule_AddIntConstant(m, "PERM_ADD_FILE", ZFSACE_ADD_FILE);
	PyModule_AddIntConstant(m, "PERM_APPEND_DATA", ZFSACE_APPEND_DATA);
	PyModule_AddIntConstant(m, "PERM_ADD_SUBDIRECTORY",
	    ZFSACE_ADD_SUBDIRECTORY);
	PyModule_AddIntConstant(m, "PERM_READ_NAMED_ATTRS",
	    ZFSACE_READ_NAMED_ATTRS);
	PyModule_AddIntConstant(m, "PERM_WRITE_NAMED_ATTRS",
	    ZFSACE_WRITE_NAMED_ATTRS);
	PyModule_AddIntConstant(m, "PERM_DELETE_CHILD", ZFSACE_DELETE_CHILD);
	PyModule_AddIntConstant(m, "PERM_READ_ATTRIBUTES",
	    ZFSACE_READ_ATTRIBUTES);
	PyModule_AddIntConstant(m, "PERM_WRITE_ATTRIBUTES",
	    ZFSACE_WRITE_ATTRIBUTES);
	PyModule_AddIntConstant(m, "PERM_DELETE", ZFSACE_DELETE);
	PyModule_AddIntConstant(m, "PERM_READ_ACL", ZFSACE_READ_ACL);
	PyModule_AddIntConstant(m, "PERM_WRITE_ACL", ZFSACE_WRITE_ACL);
	PyModule_AddIntConstant(m, "PERM_WRITE_OWNER", ZFSACE_WRITE_OWNER);
	PyModule_AddIntConstant(m, "PERM_SYNCHRONIZE", ZFSACE_SYNCHRONIZE);
	PyModule_AddIntConstant(m, "BASIC_PERM_FULL_CONTROL", ZFSACE_FULL_SET);
	PyModule_AddIntConstant(m, "BASIC_PERM_MODIFY", ZFSACE_MODIFY_SET);
	PyModule_AddIntConstant(m, "BASIC_PERM_READ",
	    ZFSACE_READ_SET | ZFSACE_EXECUTE);
	PyModule_AddIntConstant(m, "BASIC_PERM_TRAVERSE", ZFSACE_TRAVERSE_SET);

	PyModule_AddObject(m, "Acl", (PyObject *)&PyZfsACL);

	return (m);
}

PyMODINIT_FUNC
PyInit_libzfsacl(void)
{
	return (module_init());
}
