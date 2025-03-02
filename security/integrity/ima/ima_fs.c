// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Kylene Hall <kjhall@us.ibm.com>
 * Reiner Sailer <sailer@us.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * File: ima_fs.c
 *	implemenents security file system for reporting
 *	current measurement list and IMA statistics
 */

#include <linux/fcntl.h>
#include <linux/kernel_read_file.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/parser.h>
#include <linux/vmalloc.h>
#include <linux/ima.h>

#include "ima.h"

bool ima_canonical_fmt;
static int __init default_canonical_fmt_setup(char *str)
{
#ifdef __BIG_ENDIAN
	ima_canonical_fmt = true;
#endif
	return 1;
}
__setup("ima_canonical_fmt", default_canonical_fmt_setup);

static ssize_t ima_show_htable_value(char __user *buf, size_t count,
				     loff_t *ppos, atomic_long_t *val)
{
	char tmpbuf[32];	/* greater than largest 'long' string value */
	ssize_t len;

	len = scnprintf(tmpbuf, sizeof(tmpbuf), "%li\n", atomic_long_read(val));
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, len);
}

static ssize_t ima_show_htable_violations(struct file *filp,
					  char __user *buf,
					  size_t count, loff_t *ppos)
{
	struct ima_namespace *ns = ima_ns_from_file(filp);

	if (!ns_is_active(ns))
		return -EACCES;

	return ima_show_htable_value(buf, count, ppos,
				     &ns->ima_htable.violations);
}

static const struct file_operations ima_htable_violations_ops = {
	.read = ima_show_htable_violations,
	.llseek = generic_file_llseek,
};

static ssize_t ima_show_measurements_count(struct file *filp,
					   char __user *buf,
					   size_t count, loff_t *ppos)
{
	struct ima_namespace *ns = ima_ns_from_file(filp);

	if (!ns_is_active(ns))
		return -EACCES;

	return ima_show_htable_value(buf, count, ppos, &ns->ima_htable.len);
}

static const struct file_operations ima_measurements_count_ops = {
	.read = ima_show_measurements_count,
	.llseek = generic_file_llseek,
};

/* returns pointer to hlist_node */
static void *ima_measurements_start(struct seq_file *m, loff_t *pos)
{
	struct ima_namespace *ns = ima_ns_from_file(m->file);
	loff_t l = *pos;
	struct ima_queue_entry *qe;

	/* we need a lock since pos could point beyond last element */
	rcu_read_lock();
	list_for_each_entry_rcu(qe, &ns->ima_measurements, later) {
		if (!l--) {
			rcu_read_unlock();
			return qe;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static void *ima_measurements_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ima_namespace *ns = ima_ns_from_file(m->file);
	struct ima_queue_entry *qe = v;

	/* lock protects when reading beyond last element
	 * against concurrent list-extension
	 */
	rcu_read_lock();
	qe = list_entry_rcu(qe->later.next, struct ima_queue_entry, later);
	rcu_read_unlock();
	(*pos)++;

	return (&qe->later == &ns->ima_measurements) ? NULL : qe;
}

static void ima_measurements_stop(struct seq_file *m, void *v)
{
}

void ima_putc(struct seq_file *m, void *data, int datalen)
{
	while (datalen--)
		seq_putc(m, *(char *)data++);
}

/* print format:
 *       32bit-le=pcr#
 *       char[20]=template digest
 *       32bit-le=template name size
 *       char[n]=template name
 *       [eventdata length]
 *       eventdata[n]=template specific data
 */
int ima_measurements_show(struct seq_file *m, void *v)
{
	/* the list never shrinks, so we don't need a lock here */
	struct ima_queue_entry *qe = v;
	struct ima_template_entry *e;
	char *template_name;
	u32 pcr, namelen, template_data_len; /* temporary fields */
	bool is_ima_template = false;
	int i;

	/* get entry */
	e = qe->entry;
	if (e == NULL)
		return -1;

	template_name = (e->template_desc->name[0] != '\0') ?
	    e->template_desc->name : e->template_desc->fmt;

	/*
	 * 1st: PCRIndex
	 * PCR used defaults to the same (config option) in
	 * little-endian format, unless set in policy
	 */
	pcr = !ima_canonical_fmt ? e->pcr : (__force u32)cpu_to_le32(e->pcr);

	/* 2nd: template digest */
	ima_putc(m, e->digests[ima_sha1_idx].digest, TPM_DIGEST_SIZE);

	/* 3rd: template name size */
	namelen = !ima_canonical_fmt ? strlen(template_name) :
		(__force u32)cpu_to_le32(strlen(template_name));
	ima_putc(m, &namelen, sizeof(namelen));

	/* 4th:  template name */
	ima_putc(m, template_name, strlen(template_name));

	/* 5th:  template length (except for 'ima' template) */
	if (strcmp(template_name, IMA_TEMPLATE_IMA_NAME) == 0)
		is_ima_template = true;

	if (!is_ima_template) {
		template_data_len = !ima_canonical_fmt ? e->template_data_len :
			(__force u32)cpu_to_le32(e->template_data_len);
		ima_putc(m, &template_data_len, sizeof(e->template_data_len));
	}

	/* 6th:  template specific data */
	for (i = 0; i < e->template_desc->num_fields; i++) {
		enum ima_show_type show = IMA_SHOW_BINARY;
		const struct ima_template_field *field =
			e->template_desc->fields[i];

		if (is_ima_template && strcmp(field->field_id, "d") == 0)
			show = IMA_SHOW_BINARY_NO_FIELD_LEN;
		if (is_ima_template && strcmp(field->field_id, "n") == 0)
			show = IMA_SHOW_BINARY_OLD_STRING_FMT;
		field->field_show(m, show, &e->template_data[i]);
	}
	return 0;
}

static const struct seq_operations ima_measurments_seqops = {
	.start = ima_measurements_start,
	.next = ima_measurements_next,
	.stop = ima_measurements_stop,
	.show = ima_measurements_show
};

static int ima_measurements_open(struct inode *inode, struct file *file)
{
	struct ima_namespace *ns = ima_ns_from_file(file);

	if (!ns_is_active(ns))
		return -EACCES;

	return seq_open(file, &ima_measurments_seqops);
}

static const struct file_operations ima_measurements_ops = {
	.open = ima_measurements_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

void ima_print_digest(struct seq_file *m, u8 *digest, u32 size)
{
	u32 i;

	for (i = 0; i < size; i++)
		seq_printf(m, "%02x", *(digest + i));
}

/* print in ascii */
static int ima_ascii_measurements_show(struct seq_file *m, void *v)
{
	/* the list never shrinks, so we don't need a lock here */
	struct ima_queue_entry *qe = v;
	struct ima_template_entry *e;
	char *template_name;
	int i;

	/* get entry */
	e = qe->entry;
	if (e == NULL)
		return -1;

	template_name = (e->template_desc->name[0] != '\0') ?
	    e->template_desc->name : e->template_desc->fmt;

	/* 1st: PCR used (config option) */
	seq_printf(m, "%2d ", e->pcr);

	/* 2nd: SHA1 template hash */
	ima_print_digest(m, e->digests[ima_sha1_idx].digest, TPM_DIGEST_SIZE);

	/* 3th:  template name */
	seq_printf(m, " %s", template_name);

	/* 4th:  template specific data */
	for (i = 0; i < e->template_desc->num_fields; i++) {
		seq_puts(m, " ");
		if (e->template_data[i].len == 0)
			continue;

		e->template_desc->fields[i]->field_show(m, IMA_SHOW_ASCII,
							&e->template_data[i]);
	}
	seq_puts(m, "\n");
	return 0;
}

static const struct seq_operations ima_ascii_measurements_seqops = {
	.start = ima_measurements_start,
	.next = ima_measurements_next,
	.stop = ima_measurements_stop,
	.show = ima_ascii_measurements_show
};

static int ima_ascii_measurements_open(struct inode *inode, struct file *file)
{
	struct ima_namespace *ns = ima_ns_from_file(file);

	if (!ns_is_active(ns))
		return -EACCES;

	return seq_open(file, &ima_ascii_measurements_seqops);
}

static const struct file_operations ima_ascii_measurements_ops = {
	.open = ima_ascii_measurements_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static ssize_t ima_read_policy(struct user_namespace *user_ns, char *path)
{
	void *data = NULL;
	char *datap;
	size_t size;
	int rc, pathlen = strlen(path);

	char *p;

	/* remove \n */
	datap = path;
	strsep(&datap, "\n");

	rc = kernel_read_file_from_path(path, 0, &data, INT_MAX, NULL,
					READING_POLICY);
	if (rc < 0) {
		pr_err("Unable to open file: %s (%d)", path, rc);
		return rc;
	}
	size = rc;
	rc = 0;

	datap = data;
	while (size > 0 && (p = strsep(&datap, "\n"))) {
		pr_debug("rule: %s\n", p);
		rc = ima_parse_add_rule(user_ns, p);
		if (rc < 0)
			break;
		size -= rc;
	}

	vfree(data);
	if (rc < 0)
		return rc;
	else if (size)
		return -EINVAL;
	else
		return pathlen;
}

static ssize_t ima_write_policy(struct file *file, const char __user *buf,
				size_t datalen, loff_t *ppos)
{
	struct user_namespace *user_ns = ima_user_ns_from_file(file);
	struct ima_namespace *ns = ima_ns_from_user_ns(user_ns);
	char *data;
	ssize_t result;

	if (!ns_is_active(ns))
		return -EACCES;

	if (datalen >= PAGE_SIZE)
		datalen = PAGE_SIZE - 1;

	/* No partial writes. */
	result = -EINVAL;
	if (*ppos != 0)
		goto out;

	data = memdup_user_nul(buf, datalen);
	if (IS_ERR(data)) {
		result = PTR_ERR(data);
		goto out;
	}

	result = mutex_lock_interruptible(&ns->ima_write_mutex);
	if (result < 0)
		goto out_free;

	if (data[0] == '/') {
		result = ima_read_policy(user_ns, data);
	} else if (ns == &init_ima_ns &&
		   (ima_appraise & IMA_APPRAISE_POLICY)) {
		pr_err("signed policy file (specified as an absolute pathname) required\n");
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL, NULL,
				    "policy_update", "signed policy required",
				    1, 0);
		result = -EACCES;
	} else {
		result = ima_parse_add_rule(user_ns, data);
	}
	mutex_unlock(&ns->ima_write_mutex);
out_free:
	kfree(data);
out:
	if (result < 0)
		ns->valid_policy = 0;

	return result;
}

enum ima_fs_flags {
	IMA_FS_BUSY,
};

#ifdef	CONFIG_IMA_READ_POLICY
static const struct seq_operations ima_policy_seqops = {
		.start = ima_policy_start,
		.next = ima_policy_next,
		.stop = ima_policy_stop,
		.show = ima_policy_show,
};
#endif

/*
 * ima_open_policy: sequentialize access to the policy file
 */
static int ima_open_policy(struct inode *inode, struct file *filp)
{
#ifdef CONFIG_IMA_READ_POLICY
	struct user_namespace *user_ns = ima_user_ns_from_file(filp);
#endif
	struct ima_namespace *ns = ima_ns_from_file(filp);

	if (!ns_is_active(ns))
		return -EACCES;

	if (!(filp->f_flags & O_WRONLY)) {
#ifndef	CONFIG_IMA_READ_POLICY
		return -EACCES;
#else
		if ((filp->f_flags & O_ACCMODE) != O_RDONLY)
			return -EACCES;
		if (!mac_admin_ns_capable(user_ns))
			return -EPERM;
		return seq_open(filp, &ima_policy_seqops);
#endif
	}
	if (test_and_set_bit(IMA_FS_BUSY, &ns->ima_fs_flags))
		return -EBUSY;
	return 0;
}

/*
 * ima_release_policy - start using the new measure policy rules.
 *
 * Initially, ima_measure points to the default policy rules, now
 * point to the new policy rules, and remove the securityfs policy file,
 * assuming a valid policy.
 */
static int ima_release_policy(struct inode *inode, struct file *file)
{
	struct ima_namespace *ns = ima_ns_from_file(file);
	const char *cause = ns->valid_policy ? "completed" : "failed";

	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		return seq_release(inode, file);

	if (ns->valid_policy && ima_check_policy(ns) < 0) {
		cause = "failed";
		ns->valid_policy = 0;
	}

	if (ns == &init_ima_ns) {
		pr_info("policy update %s\n", cause);
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL, NULL,
				    "policy_update", cause, !ns->valid_policy,
				    0);
	}

	if (!ns->valid_policy) {
		ima_delete_rules(ns);
		ns->valid_policy = 1;
		clear_bit(IMA_FS_BUSY, &ns->ima_fs_flags);
		return 0;
	}

	ima_update_policy(ns);
#if !defined(CONFIG_IMA_WRITE_POLICY) && !defined(CONFIG_IMA_READ_POLICY)
	securityfs_remove(ns->ima_policy);
	ns->ima_policy = NULL;
	ns->ima_policy_removed = true;
#elif defined(CONFIG_IMA_WRITE_POLICY)
	clear_bit(IMA_FS_BUSY, &ns->ima_fs_flags);
#elif defined(CONFIG_IMA_READ_POLICY)
	inode->i_mode &= ~S_IWUSR;
#endif
	return 0;
}

static const struct file_operations ima_measure_policy_ops = {
	.open = ima_open_policy,
	.write = ima_write_policy,
	.read = seq_read,
	.release = ima_release_policy,
	.llseek = generic_file_llseek,
};

static ssize_t ima_show_active(struct file *filp,
			       char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct ima_namespace *ns = ima_ns_from_file(filp);
	char tmpbuf[2];
	ssize_t len;

	len = scnprintf(tmpbuf, sizeof(tmpbuf),
			"%d\n", !!test_bit(IMA_NS_ACTIVE, &ns->ima_ns_flags));
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, len);
}

static ssize_t ima_write_active(struct file *filp,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct ima_namespace *ns = ima_ns_from_file(filp);
	unsigned int active;
	char *kbuf;
	int err;

	if (ns_is_active(ns))
		return -EBUSY;

	/* accepting '1\n' and '1\0' and no partial writes */
	if (count >= 3 || *ppos != 0)
		return -EINVAL;

	kbuf = memdup_user_nul(buf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	err = kstrtouint(kbuf, 10, &active);
	kfree(kbuf);
	if (err)
		return err;

	if (active != 1)
		return -EINVAL;

	err = ima_init_namespace(ns);
	if (err)
		return -EINVAL;

	return count;
}

static const struct file_operations ima_active_ops = {
	.read = ima_show_active,
	.write = ima_write_active,
};

int ima_fs_ns_init(struct user_namespace *user_ns, struct dentry *root)
{
	struct ima_namespace *ns = ima_ns_from_user_ns(user_ns);
	struct dentry *int_dir;
	struct dentry *ima_dir = NULL;
	struct dentry *ima_symlink = NULL;
	struct dentry *binary_runtime_measurements = NULL;
	struct dentry *ascii_runtime_measurements = NULL;
	struct dentry *runtime_measurements_count = NULL;
	struct dentry *violations = NULL;
	struct dentry *active = NULL;
	int ret;

	/*
	 * While multiple superblocks can exist they are keyed by userns in
	 * s_fs_info for securityfs. The first time a userns mounts a
	 * securityfs instance we lazily allocate the ima_namespace for the
	 * userns since that's the only way a userns can meaningfully use ima.
	 * The vfs ensures we're the only one to call fill_super() and hence
	 * ima_fs_ns_init(), so we don't need any memory barriers here, i.e.
	 * user_ns->ima_ns can't change while we're in here.
	 */
	if (!ns) {
		ns = create_ima_ns();
		if (IS_ERR(ns))
			return PTR_ERR(ns);
	}

	/* FIXME: update when evm and integrity are namespaced */
	if (user_ns != &init_user_ns) {
		int_dir = securityfs_create_dir("integrity", root);
		if (IS_ERR(int_dir)) {
			ret = PTR_ERR(int_dir);
			goto free_ns;
		}
	} else {
		int_dir = integrity_dir;
	}

	ima_dir = securityfs_create_dir("ima", int_dir);
	if (IS_ERR(ima_dir)) {
		ret = PTR_ERR(ima_dir);
		goto out;
	}

	ima_symlink = securityfs_create_symlink("ima", root, "integrity/ima",
						NULL);
	if (IS_ERR(ima_symlink)) {
		ret = PTR_ERR(ima_symlink);
		goto out;
	}

	binary_runtime_measurements =
	    securityfs_create_file("binary_runtime_measurements",
				   S_IRUSR | S_IRGRP, ima_dir, NULL,
				   &ima_measurements_ops);
	if (IS_ERR(binary_runtime_measurements)) {
		ret = PTR_ERR(binary_runtime_measurements);
		goto out;
	}

	ascii_runtime_measurements =
	    securityfs_create_file("ascii_runtime_measurements",
				   S_IRUSR | S_IRGRP, ima_dir, NULL,
				   &ima_ascii_measurements_ops);
	if (IS_ERR(ascii_runtime_measurements)) {
		ret = PTR_ERR(ascii_runtime_measurements);
		goto out;
	}

	runtime_measurements_count =
	    securityfs_create_file("runtime_measurements_count",
				   S_IRUSR | S_IRGRP, ima_dir, NULL,
				   &ima_measurements_count_ops);
	if (IS_ERR(runtime_measurements_count)) {
		ret = PTR_ERR(runtime_measurements_count);
		goto out;
	}

	violations =
	    securityfs_create_file("violations", S_IRUSR | S_IRGRP,
				   ima_dir, NULL, &ima_htable_violations_ops);
	if (IS_ERR(violations)) {
		ret = PTR_ERR(violations);
		goto out;
	}

	if (!ns->ima_policy_removed) {
		ns->ima_policy =
		    securityfs_create_file("policy", POLICY_FILE_FLAGS,
					   ima_dir, NULL,
					   &ima_measure_policy_ops);
		if (IS_ERR(ns->ima_policy)) {
			ret = PTR_ERR(ns->ima_policy);
			goto out;
		}
	}

	if (ns != &init_ima_ns) {
		active =
		    securityfs_create_file("active",
					   S_IRUSR | S_IWUSR | S_IRGRP, ima_dir,
					   NULL, &ima_active_ops);
		if (IS_ERR(active)) {
			ret = PTR_ERR(active);
			goto out;
		}
	}

	if (!ima_ns_from_user_ns(user_ns))
		user_ns_set_ima_ns(user_ns, ns);

	return 0;
out:
	securityfs_remove(active);
	securityfs_remove(ns->ima_policy);
	securityfs_remove(violations);
	securityfs_remove(runtime_measurements_count);
	securityfs_remove(ascii_runtime_measurements);
	securityfs_remove(binary_runtime_measurements);
	securityfs_remove(ima_symlink);
	securityfs_remove(ima_dir);
	if (user_ns != &init_user_ns)
		securityfs_remove(int_dir);

free_ns:
	if (!ima_ns_from_user_ns(user_ns))
		ima_free_ima_ns(ns);

	return ret;
}

int __init ima_fs_init(void)
{
	return ima_fs_ns_init(&init_user_ns, NULL);
}
