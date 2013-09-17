/*****************************************************************************\
 *  switch_cray.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if     HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <inttypes.h>
#include <fcntl.h>
#include "limits.h"
#include <linux/limits.h>
#include <numa.h>
#include <sched.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/plugins/switch/cray/switch_cray.h"
#include "src/common/pack.h"
#include "src/plugins/switch/cray/alpscomm_cn.h"
#include "src/plugins/switch/cray/alpscomm_sn.h"
#include "src/common/gres.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for SLURM switch) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "switch CRAY plugin";
const char plugin_type[]        = "switch/cray";
const uint32_t plugin_version   = 100;

// TODO: Remove once bug fix is in.
unsigned int numa_bitmask_weight(const struct bitmask *bmp);

static void print_alpsc_peInfo(alpsc_peInfo_t alps_info) {
	int i;
	info("*************************alpsc_peInfo Start*************************");
	info("totalPEs: %d\nfirstPeHere: %d\npesHere: %d\npeDepth: %d\n",
			alps_info.totalPEs, alps_info.firstPeHere, alps_info.pesHere, alps_info.peDepth);
	for (i=0; i < alps_info.totalPEs; i++) {
		info("Task: %d\tNode: %d", i, alps_info.peNidArray[i]);
	}
	info("*************************alpsc_peInfo Stop*************************");
}

static void _print_jobinfo(slurm_cray_jobinfo_t *job)
{
	int i, j, rc, cnt;
	int32_t *nodes;



	if (NULL == job) {
		error("(%s: %d: %s) job pointer was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	info("Address of slurm_cray_jobinfo_t structure: %p", job);
	info("--Begin Jobinfo--");
	info("  Magic: %" PRIx32, job->magic);
	info("  APID: %" PRIu64, job->apid);
	info("  PMI Port: %" PRIu32, job->port);
	info("  num_cookies: %" PRIu32, job->num_cookies);
	info("  --- cookies ---");
	for (i = 0; i < job->num_cookies; i++) {
		info("  cookies[%d]: %s", i, job->cookies[i]);
	}
	info("  --- cookie_ids ---");
	for (i = 0; i < job->num_cookies; i++) {
		info("  cookie_ids[%d]: %" PRIu32, i, job->cookie_ids[i]);
	}
	info("  ------");
	if (job->step_layout) {
		info("  node_cnt: %" PRIu32, job->step_layout->node_cnt);
		info("  node_list: %s", job->step_layout->node_list);
		info("  --- tasks ---");
		for (i=0; i < job->step_layout->node_cnt; i++) {
			info("  tasks[%d] = %u", i, job->step_layout->tasks[i]);
		}
		info("  ------");
		info("  task_cnt: %" PRIu32, job->step_layout->task_cnt);
		info("  --- hosts to task---");
		rc = list_str_to_array(job->step_layout->node_list, &cnt, &nodes);
		if (rc) {
			error("(%s: %d: %s) node_list_str_to_array failed", THIS_FILE, __LINE__, __FUNCTION__);
		}
		if  (job->step_layout->node_cnt != cnt) {
			error("(%s: %d: %s) list_str_to_array returned count %"
					PRIu32 "does not match expected count %d", THIS_FILE,
					__LINE__, __FUNCTION__, cnt, job->step_layout->node_cnt);
		}

		for (i=0; i < job->step_layout->node_cnt; i++) {
			info("Host: %d", i);
			for (j=0; j < job->step_layout->tasks[i]; j++) {
				info("Task: %d", job->step_layout->tids[i][j]);
			}
		}
		info("  ------");
	}
	info("--END Jobinfo--");
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init(void)
{
	verbose("%s loaded, really, really loaded.", plugin_name);
	return SLURM_SUCCESS;
}

int fini(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_reconfig(void)
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
int switch_p_libstate_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

int switch_p_libstate_restore(char *dir_name, bool recover)
{
	return SLURM_SUCCESS;
}

int switch_p_libstate_clear(void)
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo(switch_jobinfo_t **switch_job,
			   uint32_t job_id, uint32_t step_id)
{
	slurm_cray_jobinfo_t *new;

	xassert(switch_job != NULL);
	new = (slurm_cray_jobinfo_t *) xmalloc(sizeof(slurm_cray_jobinfo_t));
	new->magic = CRAY_JOBINFO_MAGIC;
	new->num_cookies = 0;
	new->cookies = NULL;
	new->cookie_ids = NULL;
	new->jobid = job_id;
	new->stepid = step_id;
	new->apid = SLURM_ID_HASH(job_id, step_id);
	new->step_layout = NULL;
	*switch_job = (switch_jobinfo_t *)new;
	return SLURM_SUCCESS;
}

int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
			   slurm_step_layout_t *step_layout,
			   char *network)
{

	int i, rc, cnt;
	uint32_t port = 0;
	int num_cookies = 2;
	char *errMsg = NULL;
	char **cookies = NULL, **s_cookies = NULL;
	int32_t *nodes = NULL, *cookie_ids = NULL;
	uint32_t *s_cookie_ids = NULL;
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)switch_job;

	if (NULL == switch_job) {
		error("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	rc = list_str_to_array(step_layout->node_list, &cnt, &nodes);

	if (rc < 0) {
		error("(%s: %d: %s) list_str_to_array failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	if  (step_layout->node_cnt != cnt) {
		error("(%s: %d: %s) list_str_to_array returned count %"
				PRIu32 "does not match expected count %d", THIS_FILE,
				__LINE__, __FUNCTION__, cnt, job->step_layout->node_cnt);
	}

	/*
	 * Get cookies for network configuration
	 *
	 * TODO: I could specify a lease time if I knew the wall-clock limit of
	 * the job.  However, if the job got suspended, then all bets are off.  An
	 * infinite release time seems safest for now.
	 *
	 * TODO: The domain parameter should be something to identify the job such as the
	 * APID.  I left it as zero for now because I don't know how to get the
	 * JOB ID and JOB STEP ID from here.
	 *
	 * TODO: I'm hard-coding the number of cookies for now to two.  Maybe we'll
	 * have a dynamic way to ascertain the number of cookies later.
	 *
	 * TODO: I could ensure that the nodes list was sorted either by doing
	 * some research to see if it comes in sorted or calling a sort
	 * routine.
	 */
	rc = alpsc_lease_cookies(&errMsg,
				       "SLURM", job->apid,
				       ALPSC_INFINITE_LEASE, nodes,
				       step_layout->node_cnt, num_cookies,
				       &cookies, &cookie_ids);
	if (rc != 0) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_lease_cookies failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_lease_cookies failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		xfree(nodes);
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_lease_cookies: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}

	xfree(nodes);

	/*
	 * xmalloc the space for the cookies and cookie_ids, so it can be freed
	 * with xfree later, which is consistent with SLURM practices and how the
	 * rest of the structure will be freed.
	 * We must free() the ALPS Common library allocated memory using free(),
	 * not xfree().
	 */
	s_cookie_ids = (uint32_t *) xmalloc(sizeof(uint32_t) * num_cookies);
	memcpy(s_cookie_ids, cookie_ids, sizeof(uint32_t) * num_cookies);
	free(cookie_ids);

	s_cookies = (char **) xmalloc(sizeof(char **) * num_cookies);
	for (i=0; i<num_cookies; i++) {
		s_cookies[i] = xstrdup(cookies[i]);
		free(cookies[i]);
	}
	free(cookies);

	/*
	 * Get a unique port for PMI communications
	 */
	 rc = assign_port(&port);
	if (rc < 0) {
		info("(%s: %d: %s) assign_port failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	/*
	 * Populate the switch_jobinfo_t struct
	 * Make a copy of the step_layout, so that switch_p_free_jobinfo can
	 * consistently free it later whether it's dealing with a copy that was
	 * created by this function or any other like switch_p_copy_jobinfo or
	 * switch_p_unpack_jobinfo.
	 */
	job->num_cookies = num_cookies;
	job->cookies = s_cookies;
	job->cookie_ids = s_cookie_ids;
	job->port = port;
	job->step_layout = slurm_step_layout_copy(step_layout);

	/*
	 * Inform the system that an application (i.e. job step) is starting.
	 * This is for tracking purposes for congestion management and power
	 * management.
	 * This is probably going to be moved to the select plugin.
	 *
	 * TODO: Implement the actual call.
	 */
	// alpsc_put_app_start_info();

	return SLURM_SUCCESS;
}

switch_jobinfo_t *switch_p_copy_jobinfo(switch_jobinfo_t *switch_job)
{
	int i;
	slurm_cray_jobinfo_t *old = (slurm_cray_jobinfo_t *)switch_job;
	switch_jobinfo_t *new_init;
	slurm_cray_jobinfo_t *new;
	size_t sz;

	if (NULL == switch_job) {
		error("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return NULL;
	}
	xassert(((slurm_cray_jobinfo_t *)switch_job)->magic == CRAY_JOBINFO_MAGIC);

	if (switch_p_alloc_jobinfo(&new_init, old->jobid, old->stepid)) {
		error("Allocating new jobinfo");
		slurm_seterrno(ENOMEM);
		return NULL;
	}

	new = (slurm_cray_jobinfo_t *)new_init;
	// Copy over non-malloced memory.
	*new = *old;

	new->cookies = (char **) xmalloc(old->num_cookies * sizeof(char **));
	for(i=0; i<old->num_cookies; i++) {
		new->cookies[i] = xstrdup(old->cookies[i]);
	}

	sz = sizeof(*(new->cookie_ids));
	new->cookie_ids = xmalloc(old->num_cookies * sz);
	memcpy(new->cookie_ids, old->cookie_ids, old->num_cookies * sz);

	new->step_layout = slurm_step_layout_copy(old->step_layout);

	return (switch_jobinfo_t *)new;
}

/*
 *
 */
void switch_p_free_jobinfo(switch_jobinfo_t *switch_job)
{
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)switch_job;
	int i;
	if (!job)
		return;

	if (job->magic != CRAY_JOBINFO_MAGIC) {
		error("job is not a switch/cray slurm_cray_jobinfo_t");
		return;
	}

	job->magic = 0;

	/*
	 * Free the cookies and the cookie_ids.
	 */
	if (job->num_cookies != 0) {
		// Free the cookie_ids
		if (job->cookie_ids) {
			xfree(job->cookie_ids);
		}

		if (job->cookies) {
			// Free the individual cookie strings.
			for (i = 0; i < job->num_cookies; i++) {
				if(job->cookies[i]) {
					xfree(job->cookies[i]);
				}
			}

			// Free the cookie array
			xfree(job->cookies);
		}
	}

	if (NULL != job->step_layout) {
		slurm_step_layout_destroy(job->step_layout);
	}

	xfree(job);

	return;
}

/*
 * pack_test
 * Description:
 * Tests the packing by doing some unpacking.
 * TO DO: I need to carefully free the memory that I allocate here.
 */
int pack_test(Buf buffer, uint32_t job_id, uint32_t step_id) {

	int rc;
	uint32_t num_cookies;
	switch_jobinfo_t *pre_job;
	slurm_cray_jobinfo_t *job;
	switch_p_alloc_jobinfo(&pre_job, job_id, step_id);
	job = (slurm_cray_jobinfo_t*) pre_job;
	xassert(job);
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	xassert(buffer);
	rc = unpack32(&job->magic, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		goto error_exit;
	}
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	/*
	 * There's some dodgy type-casting here because I'm dealing with signed
	 * integers, but the pack/unpack functions use signed integers.
	 */
	rc = unpack32(&(job->num_cookies), buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		goto error_exit;
	}
	rc = unpackstr_array(&(job->cookies), &num_cookies, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpackstr_array failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		goto error_exit;
	}
	if (num_cookies != job->num_cookies) {
		error("(%s: %d: %s) Wrong number of cookies received.  Expected: %"
				PRIu32 "Received: %" PRIu32, THIS_FILE, __LINE__, __FUNCTION__,
				job->num_cookies, num_cookies);
		goto error_exit;
	}
	rc = unpack32_array(&(job->cookie_ids), &(job->num_cookies), buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32_array failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		goto error_exit;
	}

	/*
	 * Allocate our own step_layout function.
	 */
	rc = unpack_slurm_step_layout(&(job->step_layout), buffer, SLURM_PROTOCOL_VERSION);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		goto error_exit;
	}

	info("(%s:%d: %s) switch_jobinfo_t contents:", THIS_FILE, __LINE__, __FUNCTION__);
	_print_jobinfo(job);

	return SLURM_SUCCESS;

	error_exit:
	switch_p_free_jobinfo(pre_job);
	return SLURM_ERROR;
}

/*
 * TODO: Pack job id, step id, and apid
 */
int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer,
			  uint16_t protocol_version)
{
	int i;

	slurm_cray_jobinfo_t *job= (slurm_cray_jobinfo_t *)switch_job;

	if (NULL == switch_job) {
		error("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	xassert(buffer);

	/*Debug Example
	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH)
		info("(%s:%d) job id: %u -- No nodes in bitmap of "
				"job_record!",
				THIS_FILE, __LINE__, __FUNCTION__, job_ptr->job_id);
	 */

	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		info("(%s: %d: %s) switch_jobinfo_t contents", THIS_FILE, __LINE__, __FUNCTION__);
		_print_jobinfo(job);
	}

	pack32(job->magic, buffer);
	pack32(job->num_cookies, buffer);
	packstr_array(job->cookies, job->num_cookies, buffer);

	/*
	 *  Range Checking on cookie_ids
	 *  We're using a signed integer to store the cookies and a function that
	 *  packs unsigned uint32_t's, so I'm that the cookie_ids
	 *  are not negative so that they don't underflow the uint32_t.
	 */
	for (i=0; i < job->num_cookies; i++) {
		if (job->cookie_ids[i] < 0) {
			error("(%s: %d: %s) cookie_ids is negative.",
					THIS_FILE, __LINE__, __FUNCTION__);
			return SLURM_ERROR;
		}
	}
	pack32_array(job->cookie_ids, job->num_cookies, buffer);
	pack32(job->port, buffer);
	pack_slurm_step_layout(job->step_layout, buffer, SLURM_PROTOCOL_VERSION);

	/*
	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		rc = pack_test(buffer);
		if (rc != SLURM_SUCCESS) {
			error("(%s: %d: %s) pack_test failed.",
					THIS_FILE, __LINE__, __FUNCTION__);
			return SLURM_ERROR;
		}
	}
	*/
	return 0;
}

/*
 * TODO: Unpack job id, step id, and apid
 */

int switch_p_unpack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer,
			    uint16_t protocol_version)
{

	int rc;
	uint32_t num_cookies;
	/*
	char *DEBUG_WAIT=getenv("SLURM_DEBUG_WAIT");
	while(DEBUG_WAIT);
	*/

	if (NULL == switch_job) {
		error("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)switch_job;

	xassert(buffer);
	rc = unpack32(&job->magic, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	rc = unpack32(&(job->num_cookies), buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}
	rc = unpackstr_array(&(job->cookies), &num_cookies, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpackstr_array failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}
	if (num_cookies != job->num_cookies) {
		error("(%s: %d: %s) Wrong number of cookies received.  Expected: %"
				PRIu32 "Received: %" PRIu32, THIS_FILE, __LINE__, __FUNCTION__,
				job->num_cookies, num_cookies);
		return SLURM_ERROR;
	}
	rc = unpack32_array(&(job->cookie_ids), &num_cookies, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32_array failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}
	if (num_cookies != job->num_cookies) {
		error("(%s: %d: %s) Wrong number of cookie IDs received.  Expected: %"
				PRIu32 "Received: %" PRIu32, THIS_FILE, __LINE__, __FUNCTION__,
				job->num_cookies, num_cookies);
		return SLURM_ERROR;
	}

	rc = unpack32(&job->port, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	rc = unpack_slurm_step_layout(&(job->step_layout), buffer, SLURM_PROTOCOL_VERSION);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		info("(%s:%d: %s) switch_jobinfo_t contents:", THIS_FILE, __LINE__, __FUNCTION__);
		_print_jobinfo(job);
	}
       
	return SLURM_SUCCESS;
}

void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	return;
}

char *switch_p_sprint_jobinfo(switch_jobinfo_t *switch_jobinfo, char *buf,
			      size_t size)
{
	if((buf != NULL) && size) {
		buf[0] = '\0';
		return buf;
	}

	return NULL;
}

/*
 * switch functions for job initiation
 */
int switch_p_node_init(void)
{
	return SLURM_SUCCESS;
}

int switch_p_node_fini(void)
{
	return SLURM_SUCCESS;
}

int switch_p_job_preinit(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_init(stepd_step_rec_t *job)
{
	if (NULL == job) {
		error("(%s: %d: %s) job was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	slurm_cray_jobinfo_t *sw_job = (slurm_cray_jobinfo_t *)job->switch_job;
	xassert(sw_job->magic == CRAY_JOBINFO_MAGIC);
	int rc, numPTags, cmdIndex, num_app_cpus, i, j, cnt;
	int mem_scaling, cpu_scaling;
	int total_cpus = 0;
	uint32_t total_mem = 0, app_mem = 0;
	int *pTags = NULL;
	char *errMsg = NULL, *apid_dir = NULL;
	alpsc_peInfo_t alpsc_peInfo;
	FILE *f = NULL;
	size_t sz = 0;
	ssize_t lsz;
	char *lin = NULL;
	char meminfo_str[1024];
	int meminfo_value, gpu_enable = 0;
	uint32_t task;
	int32_t *task_to_nodes_map = NULL;
	int32_t *nodes = NULL;
	int32_t firstPeHere;
	gni_ntt_descriptor_t *ntt_desc_ptr = NULL;
	int gpu_cnt = 0;
	char *buff;
	int cleng = 0;

	/*
	 * 	sleep(60);
	int debug_sleep_wait = 1;
	while(debug_sleep_wait);
	*/

	// Dummy variables to satisfy alpsc_write_placement_file
	int controlNid = 0, numBranches = 0;
	struct sockaddr_in controlSoc;
	alpsc_branchInfo_t alpsc_branchInfo;


    rc = alpsc_attach_cncu_container(&errMsg, sw_job->jobid, job->cont_id);

	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_attach_cncu_container failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_attach_cncu_container failed: No error "
					"message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_configure_nic: %s", THIS_FILE, __LINE__,
				__FUNCTION__, errMsg);
		free(errMsg);
	}

	/*
	 * Create APID directory
	 * Make its owner be the user who launched the application and under which
	 * the application will run.
	 */
	rc = asprintf(&apid_dir, "/var/spool/alps/%" PRIu64, sw_job->apid);
	if (rc == -1) {
		error("(%s: %d: %s) asprintf failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	rc = mkdir(apid_dir, 700);
	if (rc) {
		free(apid_dir);
		error("(%s: %d: %s) mkdir failed: %s", THIS_FILE, __LINE__, __FUNCTION__, strerror(errno));
		return SLURM_ERROR;
	}

	rc = chown(apid_dir, job->uid, job->gid);
	if (rc) {
		free(apid_dir);
		error("(%s: %d: %s) chown failed: %s", THIS_FILE, __LINE__, __FUNCTION__, strerror(errno));
		return SLURM_ERROR;
	}

	free(apid_dir);
	/*
	 * Not defined yet -- This one may be skipped because we may not need to
	 * find the PAGG JOB container based on the APID.  It is part of the
	 * stepd_step_rec_t struct in the cont_id member, so if we have access to the
	 * struct, then we have access to the JOB container.
	 */

	// alpsc_set_PAGG_apid()

	/*
	 * Configure the network
	 *
	 * I'm setting exclusive flag to zero for now until we can figure out a way
	 * to guarantee that the application not only has exclusive access to the
	 * node but also will not be suspended.  This may not happen.
	 */

	/*
	 * To get the number of CPUs.
	 *
	 * I co-opted the hostlist_count() and its counterparts to count CPUS.
	 * TODO: There might be a better (community) way to do this.
	 */
	total_cpus = get_cpu_total();

	if (total_cpus <= 0) {
		error("(%s: %d: %s) total_cpus <=0: %d", THIS_FILE, __LINE__,
				__FUNCTION__, total_cpus);
		return SLURM_ERROR;
	}

	//Use /proc/meminfo to get the total amount of memory on the node
	f = fopen("/proc/meminfo", "r");
	if (f == NULL) {
		error("(%s: %d: %s) Failed to open /proc/meminfo: %s", THIS_FILE,
				__LINE__, __FUNCTION__, strerror(errno));
		return SLURM_ERROR;
	}

	while (!feof(f)) {
		lsz = getline(&lin, &sz, f);
		if (lsz > 0) {
			sscanf(lin, "%s %d", meminfo_str, &meminfo_value);
			if(!strcmp(meminfo_str, "MemTotal:")) {
				total_mem = meminfo_value;
				break;
			}
		}
	}
	free(lin);
	fclose(f);

	if (total_mem == 0) {
		error("(%s: %d: %s) Scanning /proc/meminfo results in MemTotal=0", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	// Scaling
	num_app_cpus = job->node_tasks * job->cpus_per_task;
	if (num_app_cpus  <= 0) {
		error("(%s: %d: %s) num_app_cpus <=0: %d", THIS_FILE, __LINE__,
				__FUNCTION__, num_app_cpus );
		return SLURM_ERROR;
	}

	cpu_scaling = ((double)num_app_cpus / (double)total_cpus ) * 100;
	if ((cpu_scaling <= 0) || (cpu_scaling > 100)) {
		error("(%s: %d: %s) Cpu scaling out of bounds: %d", THIS_FILE,
				__LINE__, __FUNCTION__, cpu_scaling);
		return SLURM_ERROR;
	}
	if (job->step_mem & MEM_PER_CPU) {
		/*
		 * This means that job->step_mem is the amount of memory per CPU,
		 * not total.
		 */
		app_mem = (job->step_mem * num_app_cpus);
	} else {
		app_mem = job->step_mem;
	}

	/*
	 * Scale total_mem, which is in kilobytes, to megabytes because app_mem is
	 * in megabytes.
	 */
	mem_scaling = ((double) app_mem / ((double) total_mem / 1024)) * 100;
	if ((mem_scaling <= 0) || (mem_scaling > 100)) {
		error("(%s: %d: %s) Memory scaling out of bounds: %d", THIS_FILE,
				__LINE__, __FUNCTION__, mem_scaling);
		return SLURM_ERROR;
	}


	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		info("(%s:%d: %s) --Network Scaling Start--", THIS_FILE, __LINE__,
				__FUNCTION__);
		info("(%s:%d: %s) --CPU Scaling: %d--", THIS_FILE, __LINE__,
				__FUNCTION__, cpu_scaling);
		info("(%s:%d: %s) --Memory Scaling: %d--", THIS_FILE, __LINE__,
						__FUNCTION__, mem_scaling);
		info("(%s:%d: %s) --Network Scaling End--", THIS_FILE, __LINE__,
				__FUNCTION__);

		info("(%s:%d: %s) --PAGG Job Container ID: %" PRIx64 "--", THIS_FILE, __LINE__,
						__FUNCTION__, job->cont_id);
	}

	rc = alpsc_configure_nic(&errMsg, 0, cpu_scaling,
	    mem_scaling, job->cont_id, sw_job->num_cookies,
	    (const char **)sw_job->cookies, &numPTags, &pTags, ntt_desc_ptr);
	/*
	 * We don't use the pTags because Cray's LLI acquires them itself, so they
	 * can be immediately discarded.
	 */
	free(pTags);
	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_configure_nic failed: %s", THIS_FILE,
					__LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_configure_nic failed: No error message "
					"present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_configure_nic: %s", THIS_FILE, __LINE__,
				__FUNCTION__, errMsg);
		free(errMsg);
	}


	// Not defined yet -- deferred
	//alpsc_config_gpcd();

	/*
	 * The following section will fill in the alpsc_peInfo structure which is
	 * the key argument to the alpsc_write_placement_file() call.
	 */
	alpsc_peInfo.totalPEs = job->ntasks;
	alpsc_peInfo.pesHere = job->node_tasks;
	alpsc_peInfo.peDepth = job->cpus_per_task;

	/*
	 * Fill in alpsc_peInfo.firstPeHere
	 */
	rc = get_first_pe(job->nodeid, job->node_tasks,
			sw_job->step_layout->tids, &firstPeHere);
	if (rc < 0) {
		error("(%s: %d: %s) get_first_pe failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	alpsc_peInfo.firstPeHere = firstPeHere;

	/*
	 * Fill in alpsc_peInfo.peNidArray
	 *
	 * The peNidArray maps tasks to nodes.
	 * Basically, reverse the tids variable which maps nodes to tasks.
	 */
	rc = list_str_to_array(sw_job->step_layout->node_list, &cnt, &nodes);

	if (rc < 0) {
		error("(%s: %d: %s) list_str_to_array failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	if  (sw_job->step_layout->node_cnt != cnt) {
		error("(%s: %d: %s) list_str_to_array returned count %"
				PRIu32 "does not match expected count %d", THIS_FILE,
				__LINE__, __FUNCTION__, cnt, sw_job->step_layout->node_cnt);
	}

	task_to_nodes_map = xmalloc(sw_job->step_layout->task_cnt * sizeof(int32_t));

	for (i=0; i<sw_job->step_layout->node_cnt; i++) {
		for (j=0; j < sw_job->step_layout->tasks[i]; j++) {
			task = sw_job->step_layout->tids[i][j];
			task_to_nodes_map[task] = nodes[i];
			if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
				info("(%s:%d: %s) peNidArray:\tTask: %d\tNode: %d", THIS_FILE,
						__LINE__, __FUNCTION__, task, nodes[i]);
			}
		}
	}
	alpsc_peInfo.peNidArray = task_to_nodes_map;
	xfree(nodes);

	/*
	 * Fill in alpsc_peInfo.peCmdMapArray
	 *
	 * If the job is an SPMD job, then the command Index (cmdIndex) is 0.
	 * Otherwise, if the job is an MPMD job, then the command Index (cmdIndex)
	 * is equal to the number of executables in the job minus 1.
	 *
	 * TODO: Add MPMD support once SchedMD provides the needed MPMD data.
	 */

	if (!job->multi_prog) {
		/* SPMD Launch */
		cmdIndex = 0;

		alpsc_peInfo.peCmdMapArray = calloc(alpsc_peInfo.totalPEs, sizeof(int));
	} else {
		/* MPMD Launch */

		// Deferred support
		/*
			ap.cmdIndex = ;
			ap.peCmdMapArray = ;
			ap.firstPeHere = ;

			ap.pesHere = pesHere; // These need to be MPMD specific.
		 */

		error("(%s: %d: %s) MPMD Applications are not currently supported.", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	/*
	 * Fill in alpsc_peInfo.nodeCpuArray
	 * I don't know how to get this information from SLURM.
	 * Cray's PMI does not need the information.
	 * It may be used by debuggers like ATP or lgdb.  If so, then it will
	 * have to be filled in when support for them is added.
	 *
	 * Currently, it's all zeros.
	 *
	 */
	alpsc_peInfo.nodeCpuArray = calloc(sizeof(int), sw_job->step_layout->node_cnt);
	if (sw_job->step_layout->node_cnt && (alpsc_peInfo.nodeCpuArray == NULL)) {
		free(alpsc_peInfo.peCmdMapArray);
		error("(%s: %d: %s) failed to calloc nodeCpuArray.", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	/*
	 * Some of the input parameters for alpsc_write_placement_file do not apply
	 * for SLURM.  These parameters will be given zero values.
	 * They are
	 *  int controlNid
	 *  struct sockaddr_in controlSoc
	 *  int numBranches
	 *  alpsc_branchInfo_t alpsc_branchInfo
	 */
	controlSoc.sin_port = 0;
	controlSoc.sin_addr.s_addr = 0;
	/* Just assigning controlSoc because it's already zero. */
	alpsc_branchInfo.tAddr = controlSoc;
	alpsc_branchInfo.tIndex = 0;
	alpsc_branchInfo.tLen = 0;
	alpsc_branchInfo.targ = 0;

	rc = alpsc_write_placement_file(&errMsg, sw_job->apid, cmdIndex, &alpsc_peInfo,
			controlNid, controlSoc, numBranches, &alpsc_branchInfo);

	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		print_alpsc_peInfo(alpsc_peInfo);
	}

	/* Clean up */
	xfree(task_to_nodes_map);

	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_write_placement_file failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_write_placement_file failed: No error "
					"message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_write_placement_file: %s", THIS_FILE,
				__LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}

	/*
	 * Write the CRAY_NUM_COOKIES and CRAY_COOKIES variables out, too.
	 */
	rc = asprintf(&buff, "%" PRIu32, sw_job->num_cookies);
	if (-1 == rc) {
		error("(%s: %d: %s) asprintf failed", THIS_FILE, __LINE__,
				__FUNCTION__);
		return SLURM_ERROR;
	}
	rc = env_array_overwrite(&job->env,"CRAY_NUM_COOKIES", buff);
	if (rc == 0) {
		info("Failed to set env variable CRAY_NUM_COOKIES");
		free(buff);
		return SLURM_ERROR;
	}
	free(buff);

	/*
	 * Create the CRAY_COOKIES environment variable in the application's
	 * environment.
	 * Create one string containing a comma separated list of cookies.
	 */
	for (i = 0; i < sw_job->num_cookies; i++) {
		// Add one for a trailing comma or null byte.
		cleng += strlen(sw_job->cookies[i]) + 1;
	}
	buff = (char *) xmalloc(cleng * sizeof(char));
	buff[0] = '\0';
	for (i = 0; i < sw_job->num_cookies; i++) {
		if (i > 0) {
			strcat(buff, ",");
		}
		strcat(buff, sw_job->cookies[i]);
	}

	rc = env_array_overwrite(&job->env,"CRAY_COOKIES", buff);
	if (rc == 0) {
		info("Failed to set env variable CRAY_COOKIES");
		return SLURM_ERROR;
	}
	xfree(buff);

	/*
	 * Write the PMI_CONTROL_PORT
	 * Cray's PMI uses this is the port to communicate its control tree
	 * information.
	 */
	rc = asprintf(&buff, "%" PRIu32, sw_job->port);
	if (-1 == rc) {
		error("(%s: %d: %s) asprintf failed", THIS_FILE, __LINE__,
				__FUNCTION__);
		return SLURM_ERROR;
	}
	rc = env_array_overwrite(&job->env,"PMI_CONTROL_PORT", buff);
	if (rc == 0) {
		info("Failed to set env variable PMI_CONTROL_PORT");
		free(buff);
		return SLURM_ERROR;
	}
	free(buff);


	/*
	 * Query the generic resources to see if the GPU should be allocated
	 * TO DO: Determine whether the proxy should be enabled or disabled by
	 * reading the user's environment variable.
	 */

	rc = gres_get_step_info(job->step_gres_list, "gpu", 0, GRES_STEP_DATA_COUNT, &gpu_cnt);
	info("gres_cnt: %d %u", rc, gpu_cnt);
	if (gpu_cnt > 0) {
		rc = alpsc_pre_launch_GPU_mps(&errMsg, gpu_enable);
		if (rc != 1) {
			if (errMsg) {
				error("(%s: %d: %s) alpsc_prelaunch_GPU_mps failed: %s",
						THIS_FILE, __LINE__, __FUNCTION__, errMsg);
				free(errMsg);
			}
			else {
				error("(%s: %d: %s) alpsc_prelaunch_GPU_mps failed: No error "
						"message present.", THIS_FILE, __LINE__, __FUNCTION__);
			}
			return SLURM_ERROR;
		}
		if (errMsg) {
			info("(%s: %d: %s) alpsc_prelaunch_GPU_mps: %s", THIS_FILE,
					__LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
	}


	return SLURM_SUCCESS;
}

extern int switch_p_job_suspend_test(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_get(switch_jobinfo_t *jobinfo,
					  void **suspend_info)
{
	return;
}

extern void switch_p_job_suspend_info_pack(void *suspend_info, Buf buffer,
					   uint16_t protocol_version)
{
	return;
}

extern int switch_p_job_suspend_info_unpack(void **suspend_info, Buf buffer,
					    uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_free(void *suspend_info)
{
	return;
}

extern int switch_p_job_suspend(void *suspend_info, int max_wait)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_resume(void *suspend_info, int max_wait)
{
	return SLURM_SUCCESS;
}

int switch_p_job_fini(switch_jobinfo_t *jobinfo)
{

	if (NULL == jobinfo) {
		error("(%s: %d: %s) jobinfo was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)jobinfo;
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	int rc;
	char *path_name = NULL;

	/*
	 * Remove the APID directory /var/spool/alps/<APID>
	 */
	rc = asprintf(&path_name, "/var/spool/alps/%" PRIu64, job->apid);
	if (rc == -1) {
		error("(%s: %d: %s) asprintf failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	// Stolen from ALPS
	recursiveRmdir(path_name);
	free(path_name);

	/*
	 * Remove the ALPS placement file.
	 * /var/spool/alps/places<APID>
	 */
	rc = asprintf(&path_name, "/var/spool/alps/places%" PRIu64, job->apid);
	if (rc == -1) {
		error("(%s: %d: %s) asprintf failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	rc = remove(path_name);
	if (rc) {
		error("(%s: %d: %s) remove %s failed: %s", THIS_FILE, __LINE__,
				__FUNCTION__, path_name, strerror(errno));
		return SLURM_ERROR;
	}
	free(path_name);

	/*
	 * TO DO:
	 * Set the proxy back to the default state.
	 */

	return SLURM_SUCCESS;
}

int switch_p_job_postfini(stepd_step_rec_t *job)
{
	int rc, cnt;
	int32_t *numa_nodes;
	char *errMsg = NULL, path[PATH_MAX];
	cpu_set_t *cpuMasks;

	/*
	 *  TO DO -- This is just a place-holder until I can get the actual
	 *  uid of the application.
	 */
	uid_t uid = 0;
	uid_t pgid = job->jmgr_pid;

	if (NULL == job) {
		error("(%s: %d: %s) job was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu",
		       (unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		info("Job %u.%u: Bad pid value %lu", job->jobid,
		      job->stepid, (unsigned long) pgid);
	/*
	 * Clean-up
	 *
	 * 1. Flush Lustre caches
	 * 2. Flush virtual memory
	 * 3. Compact memory
	*/

	// Flush Lustre Cache
	rc = alpsc_flush_lustre(&errMsg);
	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_flush_lustre failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_flush_lustre failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_flush_lustre: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}

	// Flush virtual memory
	rc = system("echo 3 > /proc/sys/vm/drop_caches");
	if (rc != -1) {
		rc = WEXITSTATUS(rc);
	}
	if (rc) {
		error("(%s: %d: %s) Flushing virtual memory failed. Return code: %d",
				THIS_FILE, __LINE__, __FUNCTION__, rc);
	}
	// do_drop_caches();

	/*
	 * Compact Memory
	 * Determine which NUMA nodes an application is using.  Then, use that to
	 * compact the memory.
	 *
	 * You'll find the NUMA node information in the following location.
	 * /dev/cpuset/slurm/uid_<uid>/job_<jobID>/step_<stepID>/cpuset.mems
	 */

	rc = snprintf(path, sizeof(path), "/dev/cpuset/slurm/uid_%d/job_%" PRIu32
			"/step_%" PRIu32, uid, job->jobid, job->stepid);
	if (rc < 0) {
		error("(%s: %d: %s) snprintf failed. Return code: %d",
						THIS_FILE, __LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	rc = get_numa_nodes(path, &cnt, &numa_nodes);
	if (rc < 0) {
		error("(%s: %d: %s) get_numa_nodes failed. Return code: %d",
						THIS_FILE, __LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	rc = get_cpu_masks(path, &cpuMasks);
	if (rc < 0) {
		error("(%s: %d: %s) get_cpu_masks failed. Return code: %d",
				THIS_FILE, __LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	rc = alpsc_compact_mem(&errMsg, cnt, numa_nodes, cpuMasks, path);

	xfree(numa_nodes);
	xfree(cpuMasks);

	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_compact_mem failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_compact_mem failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_compact_mem: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}

	return SLURM_SUCCESS;
}

int switch_p_job_attach(switch_jobinfo_t *jobinfo, char ***env,
			uint32_t nodeid, uint32_t procid, uint32_t nnodes,
			uint32_t nprocs, uint32_t rank)
{
	return SLURM_SUCCESS;
}

extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job,
				int key, void *resulting_data)
{
	slurm_seterrno(EINVAL);
	return SLURM_ERROR;
}

/*
 * switch functions for other purposes
 */
extern int switch_p_get_errno(void)
{
	return SLURM_SUCCESS;
}

extern char *switch_p_strerror(int errnum)
{
	return NULL;
}

/*
 * node switch state monitoring functions
 * required for IBM Federation switch
 */
extern int switch_p_clear_node_state(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_alloc_node_info(switch_node_info_t **switch_node)
{
	return SLURM_SUCCESS;
}

extern int switch_p_build_node_info(switch_node_info_t *switch_node)
{
	return SLURM_SUCCESS;
}

extern int switch_p_pack_node_info(switch_node_info_t *switch_node,
				   Buf buffer, uint16_t protocol_version)
{
	return 0;
}

extern int switch_p_unpack_node_info(switch_node_info_t *switch_node,
				     Buf buffer, uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int switch_p_free_node_info(switch_node_info_t **switch_node)
{
	return SLURM_SUCCESS;
}

extern char*switch_p_sprintf_node_info(switch_node_info_t *switch_node,
				       char *buf, size_t size)
{
	if ((buf != NULL) && size) {
		buf[0] = '\0';
		return buf;
	}

	return NULL;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo,
		char *nodelist)
{
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)jobinfo;
	char *errMsg = NULL;
	int rc = 0;

	if (NULL == jobinfo) {
		error("(%s: %d: %s) jobinfo was NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		info("(%s:%d: %s) switch_p_job_step_complete", THIS_FILE, __LINE__, __FUNCTION__);
	}

	/* Release the cookies */

	rc = alpsc_release_cookies(&errMsg, (int32_t *)job->cookie_ids, (int32_t)job->num_cookies);

	if (rc != 0) {

		if (errMsg) {
			error("(%s: %d: %s) alpsc_release_cookies failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_release_cookies failed: No error message "
					"present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;

	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_release_cookies: %s", THIS_FILE, __LINE__,
				__FUNCTION__, errMsg);
		free(errMsg);
	}

	/*
	 * Release the reserved PMI port
	 * If this fails, do not exit with an error.
	 */
	rc = release_port(job->port);
	if (rc != 0) {
		error("(%s: %d: %s) Releasing port %" PRIu32 " failed.", THIS_FILE,
				__LINE__, __FUNCTION__, job->port);
		// return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int switch_p_job_step_part_comp(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	return SLURM_SUCCESS;
}

extern bool switch_p_part_comp(void)
{
	return false;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	return SLURM_SUCCESS;
}

extern int switch_p_slurmctld_init(void)
{
	int rc;
	/*
	 *  Initialize the port reservations.
	 *  Each job step will be allocated one port from amongst this set of
	 *  reservations for use by Cray's PMI for control tree communications.
	 */
	rc = init_port();
	if (rc) {
		error("(%s: %d: %s) Initializing PMI reserve port table failed",
				THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

#define ALPS_DIR "/var/opt/cray/alps/spool/"
#define LEGACY_SPOOL_DIR "/var/spool/"

extern int switch_p_slurmd_init(void)
{
	int rc = 0;
	char *errMsg = NULL;

	// Establish GPU's default state
	rc = alpsc_establish_GPU_mps_def_state(&errMsg);
	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_establish_GPU_mps_def_state failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_establish_GPU_mps_def_state failed: "
					"No error message present.", THIS_FILE, __LINE__,
					__FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_establish_GPU_mps_def_state: %s", THIS_FILE,
				__LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}

	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_step_init(void)
{
	return SLURM_SUCCESS;
}

/*
 * Function: get_first_pe
 * Description:
 * Returns the first (i.e. lowest) PE on the node.
 *
 * IN:
 * nodeid -- Index of the node in the host_to_task_map
 * task_count -- Number of tasks on the node
 * host_to_task_map -- 2D array mapping the host to its tasks
 *
 * OUT:
 * first_pe -- The first (i.e. lowest) PE on the node
 *
 * RETURN
 * 0 on success and -1 on error
 */
static int get_first_pe(uint32_t nodeid, uint32_t task_count,
		uint32_t **host_to_task_map, int32_t *first_pe) {

	int i, ret = 0;

	if (task_count == 0) {
		error("(%s: %d: %s) task_count == 0", THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
	}
	if (host_to_task_map == NULL) {
		error("(%s: %d: %s) host_to_task_map == NULL", THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
	}
	*first_pe = host_to_task_map[nodeid][0];
	for (i=0; i<task_count; i++) {
		if (host_to_task_map[nodeid][i] < *first_pe) {
			*first_pe = host_to_task_map[nodeid][i];
		}
	}
	return ret;
}

/*
 * Function: list_str_to_array
 * Description:
 * 	Convert the list string into an array of integers.
 *
 * IN list -- The list string
 * OUT cnt  -- The number of numbers in the list string
 * OUT numbers -- Array of integers;  Caller is responsible to xfree()
 *                 this.
 *
 * RETURNS
 * Returns 0 on success and -1 on failure.
 */

static int list_str_to_array(char *list, int *cnt, int32_t **numbers) {

	int32_t *item_ptr = NULL;
	hostlist_t hl;
	int i, ret = 0, num_items;
	char *str, *cptr = NULL;

	/*
	 * Create a hostlist
	 */
	if ((hl = hostlist_create(list)) == NULL) {
		error("hostlist_create error on %s",
				list);
		return -1;
	}

	num_items = hostlist_count(hl);
	*cnt = num_items;

	if (num_items == 0) {
		*numbers = NULL;
		return 0;
	}

	/*
	 * Create an integer array of item_ptr in the same order as in the list.
	 */
	item_ptr = *numbers = xmalloc(num_items * sizeof(uint32_t));
	if (item_ptr == NULL) {
		error("(%s: %d: %s) xmalloc failed", THIS_FILE, __LINE__, __FUNCTION__);
		hostlist_destroy(hl);
		return -1;
	}
	for (i = 0; i < num_items; i++) {
		// str must be freed using free(), not xfree()
		str = hostlist_shift(hl);
		if (str == NULL) {
			error("(%s: %d: %s) hostlist_shift error", THIS_FILE, __LINE__, __FUNCTION__);
			xfree(item_ptr);
			hostlist_destroy(hl);
			return -1;
		}
		cptr = strpbrk(str, "0123456789");
		if (cptr == NULL) {
			error("(%s: %d: %s) Error: Node was not recognizable: %s", THIS_FILE,
					__LINE__, __FUNCTION__, str);
			free(str);
			xfree(item_ptr);
			hostlist_destroy(hl);
			return -1;
		}
		item_ptr[i] = atoll(cptr);
		free(str);
	}

	// Clean up
	hostlist_destroy(hl);

	return ret;
}

/*
 * Recursive directory delete
 *
 * Call with a directory name and this function will delete
 * all files and directories rooted in this name. Finally
 * the named directory will be deleted.
 * If called with a file name, only that file will be deleted.
 *
 * Stolen from the ALPS code base.  I may need to write my own.
 */
static void
recursiveRmdir(const char *dirnm)
{
	int            st;
	size_t         dirnmLen, fnmLen, nameLen;
	char          *fnm = 0;
	DIR           *dirp;
	struct dirent *dir;
	struct stat    stBuf;

	/* Don't do anything if there is no directory name */
	if (dirnm == NULL) {
		return;
	}
	dirp = opendir(dirnm);
	if (!dirp) {
		if (errno == ENOTDIR) goto fileDel;
		error("Error opening directory %s", dirnm);
		return;
	}

	dirnmLen = strlen(dirnm);
	if (dirnmLen == 0) return;
	while ((dir = readdir(dirp))) {
		nameLen = strlen(dir->d_name);
		if (nameLen == 1 && dir->d_name[0] == '.') continue;
		if (nameLen == 2 && strcmp(dir->d_name, "..") == 0) continue;
		fnmLen = dirnmLen + nameLen + 2;
		free(fnm);
		fnm = malloc(fnmLen);
		snprintf(fnm, fnmLen, "%s/%s", dirnm, dir->d_name);
		st = stat(fnm, &stBuf);
		if (st < 0) {
			error("stat of %s", fnm);
			continue;
		}
		if (stBuf.st_mode & S_IFDIR) {
			recursiveRmdir(fnm);
		} else {

			st = unlink(fnm);
			if (st < 0 && errno == EISDIR) st = rmdir(fnm);
			if (st < 0 && errno != ENOENT) {
				error("(%s: %d: %s) Error removing %s", THIS_FILE, __LINE__, __FUNCTION__,
						fnm);
			}
		}
	}
	free(fnm);
	closedir(dirp);
	fileDel:
	st = unlink(dirnm);
	if (st < 0 && errno == EISDIR) st = rmdir(dirnm);
	if (st < 0 && errno != ENOENT) {
		error("(%s: %d: %s) Error removing %s", THIS_FILE, __LINE__, __FUNCTION__, dirnm);
	}
}

/*
 * Function: get_cpu_total
 * Description:
 *  Get the total number of online cpus on the node.
 *
 * RETURNS
 *  Returns the number of online cpus on the node.  On error, it returns -1.
 */
static int get_cpu_total(void) {
	FILE *f = NULL;
	char * token = NULL, *token1 = NULL, *token2 = NULL, *lin=NULL;
	char *saveptr = NULL, *saveptr1 = NULL, *endptr = NULL;
	int total = 0;
	ssize_t lsz;
	size_t sz;
	long int number1, number2;

	f = fopen("/sys/devices/system/cpu/online", "r");

	if (f == NULL) {
		error("(%s: %d: %s) Failed to open file /sys/devices/system/cpu/online:"
				" %s\n", THIS_FILE, __LINE__, __FUNCTION__, strerror(errno));
		return -1;
	}

	while (!feof(f)) {
		lsz = getline(&lin, &sz, f);
		if (lsz > 0) {
			token = strtok_r(lin, ",", &saveptr);
			while (token) {
				// Check for ranged sub-list
				token1 = strtok_r(token, "-", &saveptr1);
				if (token1) {
					number1 = strtol(token1, &endptr, 10);
					if ((number1 == LONG_MIN) || (number1 == LONG_MAX)) {
						error("(%s: %d: %s) Error: %s", THIS_FILE, __LINE__,
								__FUNCTION__, strerror(errno));
						free(lin);
						TEMP_FAILURE_RETRY(fclose(f));
						return -1;
					} else if (endptr == token1) {
						error("(%s: %d: %s) Error: Not a number: %s\n",
								THIS_FILE, __LINE__, __FUNCTION__, endptr);
						free(lin);
						TEMP_FAILURE_RETRY(fclose(f));
						return -1;
					}

					token2 = strtok_r(NULL, "-", &saveptr1);
					if(token2) {
						number2 = strtol(token2, &endptr, 10);
						if ((number2 == LONG_MIN) || (number2 == LONG_MAX)) {
							error("(%s: %d: %s) Error: %s", THIS_FILE,
									__LINE__, __FUNCTION__, strerror(errno));
							free(lin);
							TEMP_FAILURE_RETRY(fclose(f));
							return -1;
						} else if (endptr == token2) {
							error("(%s: %d: %s) Error: Not a number: '%s'\n",
									THIS_FILE, __LINE__, __FUNCTION__, endptr);
							free(lin);
							TEMP_FAILURE_RETRY(fclose(f));
							return -1;
						}

						total += number2 - number1 + 1;
					} else {
						total += 1;
					}
				}
				token = strtok_r(NULL, ",", &saveptr);
			}
		}
	}
	free(lin);
	TEMP_FAILURE_RETRY(fclose(f));
	return total;
}

#define MIN_PORT	20000
#define MAX_PORT	30000
#define ATTEMPTS	2

static uint32_t *port_resv = NULL;
static int port_cnt = -1;
static uint32_t last_alloc_port;

/*
 * Function: init_port
 * Description:
 *  Mallocs space for and initializes the reserved ports table.
 *  0 in the table means free and 1 means reserved.
 *  port_cnt is the total number of ports in the table.
 *  last_alloc_port is the last allocated port, and it is using the table's
 *  index, not the real port number.
 *
 * Returns
 *  0 on success and -1 on failure.
 *
 * TODO: Once we have the SchedMD code that actually calls
 * switch_p_slurmctld_init, I should take out the safety checks that
 * re-initialize the port reservation table.
 */

static int init_port() {

	extern uint32_t *port_resv;
	extern int port_cnt;
	extern uint32_t last_alloc_port;

	int i;
	if (MAX_PORT < MIN_PORT) {
		error("(%s: %d: %s) MAX_PORT: %d < MIN_PORT: %d", THIS_FILE, __LINE__, __FUNCTION__, MAX_PORT, MIN_PORT);
		return -1;
	}

	port_cnt = MAX_PORT - MIN_PORT;
	last_alloc_port = port_cnt;
	port_resv = xmalloc(port_cnt * sizeof(uint32_t));

	for (i=0; i<port_cnt; i++) {
		port_resv[i]=0;
	}
	return 0;
}

/*
 * Function: assign_port
 * Description:
 *  Looks for and assigns the next free port.
 *  If there are no free ports, then it loops through the entire table
 *  ATTEMPTS number of times before declaring a failure.
 * Returns:
 *  0 on success and -1 on failure.
*/
static int assign_port(uint32_t *real_port) {
	int port, tmp, attempts = 0, rc;

	if (real_port == NULL) {
		error("(%s: %d: %s) real_port address was NULL.",
				THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
	}

	if(port_resv == NULL) {
		info("(%s: %d: %s) Reserved PMI Port Table not initialized",
				THIS_FILE, __LINE__, __FUNCTION__);
		rc = init_port();
		if (rc) {
			error("(%s: %d: %s) Initializing PMI reserve port table failed",
					THIS_FILE, __LINE__, __FUNCTION__);
			return -1;
		}
		/*
		 * This is the code that I think should be here, but until we resolve
		 * when and if switch_p_slurmctld_init is called, the above is a
		 * safe-guard.
		error("(%s: %d: %s) Reserved PMI Port Table not initialized",
				THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
		*/
	}

	/*
	 * Ports is an index into the reserved port table.
	 * The ports range from 0 up to port_cnt.
	 */
	port = ++last_alloc_port % port_cnt;

	/*
	 * Find an unreserved port to assign.
	 * Abandon the attempt if we've been through the available ports ATTEMPT
	 * number of times
	 */
	while (port_resv[port]==1) {
		tmp = port++ % port_cnt;
		port = tmp;
		attempts++;
		if ((attempts / port_cnt) >= ATTEMPTS) {
			error("(%s: %d: %s) No free ports among %d ports.  Went through "
					"entire port list %d times", THIS_FILE, __LINE__,
					__FUNCTION__, port_cnt, ATTEMPTS);
			return -1;
		}
	}

	port_resv[port] = 1;
	last_alloc_port = port;

	/*
	 * The port index must be scaled up by the MIN_PORT.
	 */
	*real_port = (port + MIN_PORT);
	return 0;
}

/*
 * Function: release_port
 * Description:
 *  Release the port.
 *
 * Returns:
 *  0 on success and -1 on failure.
*/
static int release_port(uint32_t real_port) {

	int rc;
	uint32_t port;

	if ((real_port < MIN_PORT) || (real_port >= MAX_PORT)) {
		error("(%s: %d: %s) Port %" PRIu32 "outside of valid range %" PRIu32
				": %" PRIu32, THIS_FILE, __LINE__, __FUNCTION__, real_port,
				MIN_PORT, MAX_PORT);
		return -1;
	}

	port = real_port - MIN_PORT;

	if(port_resv == NULL) {
		info("(%s: %d: %s) Reserved PMI Port Table not initialized",
				THIS_FILE, __LINE__, __FUNCTION__);
		rc = init_port();
		if (rc) {
			error("(%s: %d: %s) Initializing PMI reserve port table failed",
					THIS_FILE, __LINE__, __FUNCTION__);
			return -1;
		}

		/*
		 * This is the code that I think should be here, but until we resolve
		 * when and if switch_p_slurmctld_init is called, the above is a
		 * safe-guard.
		error("(%s: %d: %s) Reserved PMI Port Table not initialized",
				THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
		*/
	}

	if (port_resv[port]) {
		port_resv[port] = 0;
	} else {
		error("(%s: %d: %s) Attempting to release port %d, but it was not "
				"reserved. ", THIS_FILE, __LINE__, __FUNCTION__, real_port);
		return -1;
	}
	return 0;
}

/*
 * Function: get_numa_nodes
 * Description:
 *  Returns a count of the NUMA nodes that the application is running on.
 *
 *  Returns an array of NUMA nodes that the application is running on.
 *
 *
 *  IN char* path -- The path to the directory containing the files containing
 *                   information about NUMA nodes.
 *
 *  OUT *cnt -- The number of NUMA nodes in the array
 *  OUT **numa_array -- An integer array containing the NUMA nodes.  This array must be xfreed
 *                      by the caller.
 *
 * RETURN
 *  0 on success and -1 on failure.
 */
static int get_numa_nodes(char *path, int *cnt, int32_t **numa_array) {
	  struct bitmask *bm;
	  int i, index, rc;
	  int lsz;
	  size_t sz;
	  char buffer[PATH_MAX];
	  FILE *f = NULL;
	  char *lin = NULL;

	  snprintf(buffer, sizeof(buffer), "%s/%s", path, "cpuset.mems");
	  if (rc < 0) {
		  error("(%s: %d: %s) snprintf failed. Return code: %d",
				  THIS_FILE, __LINE__, __FUNCTION__, rc);
	  }

	  f = fopen(buffer, "r");
	  if (f == NULL) {
		  error("Failed to open file %s: %s\n", buffer, strerror(errno));
		  return -1;
	  }

	  lsz = getline(&lin, &sz, f);
	  if (lsz > 0) {
		  bm = numa_parse_nodestring(lin);
		  if (bm == NULL) {
		    error("(%s: %d: %s)Error numa_parse_nodestring", THIS_FILE, __LINE__,
		    		__FUNCTION__);
		    free(lin);
		    return SLURM_ERROR;
		  }
	  } else {
		  error("(%s: %d: %s) Reading %s failed.", THIS_FILE, __LINE__,
				  __FUNCTION__, buffer);
		  return SLURM_ERROR;
	  }
	  free(lin);

	  *cnt = numa_bitmask_weight(bm);
	  if (*cnt == 0) {
	    error("(%s: %d: %s)Error no NUMA Nodes found.", THIS_FILE, __LINE__,
	    		__FUNCTION__);
	    return -1;
	  }

	  if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		  info("Btimask size: %lu\nSizeof(*(bm->maskp)):%zd\n"
				  "Bitmask %#lx\nBitmask weight(number of bits set): %u\n",
				  bm->size, sizeof(*(bm->maskp)), *(bm->maskp),
		         *cnt);

	  }

	  *numa_array = xmalloc(*cnt * sizeof(int32_t));
	  if (*numa_array == NULL) {
	    error("(%s: %d: %s)Error out of memory.\n", THIS_FILE, __LINE__,
	    		__FUNCTION__);
	    return -1;
	  }

	  index = 0;
	  for (i = 0; i < bm->size; i++) {
	    if (*(bm->maskp) & ((long unsigned)1 << i)) {
	    	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
	    		info("(%s: %d: %s)NUMA Node %d is present.\n", THIS_FILE,
	    				__LINE__, __FUNCTION__, i);
	    	}
	      (*numa_array)[index++] = i;
	    }
	  }

	  numa_free_nodemask(bm);

	return 0;
}

/*
 * Function: get_cpu_masks
 * Description:
 *
 *  Returns a cpu_set_t contains the masks of the CPUs within the NUMA nodes
 *  that are in use by the application.
 *
 *  IN char* path -- The path to the directory containing the files containing
 *                   information about NUMA nodes.
 *  OUT cpu_set_t **cpuMasks -- Pointer to the CPUS used by the application.
 *                              Must be freed via CPU_FREE() by the caller.
 * RETURN
 *  0 on success and -1 on failure.
 */
static int get_cpu_masks(char *path, cpu_set_t **cpuMasks) {
	struct bitmask *bm;
	int i, index, rc, cnt;
	char buffer[PATH_MAX];
	FILE *f = NULL;
	char *lin = NULL;
	int lsz;
	size_t sz;

	snprintf(buffer, sizeof(buffer), "%s/%s", path, "cpuset.cpus");
	if (rc < 0) {
		error("(%s: %d: %s) snprintf failed. Return code: %d",
				THIS_FILE, __LINE__, __FUNCTION__, rc);
		return -1;
	}

	f = fopen(buffer, "r");
	if (f == NULL) {
		error("Failed to open file %s: %s\n", buffer, strerror(errno));
		return -1;
	}

	lsz = getline(&lin, &sz, f);
	if (lsz > 0) {
		bm = numa_parse_cpustring(buffer);
		if (bm == NULL) {
			error("(%s: %d: %s)Error numa_parse_nodestring", THIS_FILE, __LINE__,
					__FUNCTION__);
			free(lin);
			return -1;
		}
	} else {
		error("(%s: %d: %s) Reading %s failed.", THIS_FILE, __LINE__,
				__FUNCTION__, buffer);
		return -1;
	}
	free(lin);

	cnt = numa_bitmask_weight(bm);
	if (cnt == 0) {
		error("(%s: %d: %s)Error no CPUs found.", THIS_FILE, __LINE__,
				__FUNCTION__);
		return -1;
	}

	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		info("Btimask size: %lu\nSizeof(*(bm->maskp)):%zd\n"
				"Bitmask %#lx\nBitmask weight(number of bits set): %u\n",
				bm->size, sizeof(*(bm->maskp)), *(bm->maskp),
				cnt);

	}

	*cpuMasks = CPU_ALLOC(cnt);

	if (*cpuMasks == NULL) {
		error("(%s: %d: %s)Error out of memory.\n", THIS_FILE, __LINE__,
				__FUNCTION__);
		return -1;
	}

	index = 0;
	for (i = 0; i < bm->size; i++) {
		if (*(bm->maskp) & ((long unsigned)1 << i)) {
			if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
				info("(%s: %d: %s)CPU %d is present.\n", THIS_FILE,
						__LINE__, __FUNCTION__, i);
			}
			CPU_SET(i, *cpuMasks);
		}
	}

	numa_free_cpumask(bm);

	return 0;
}
