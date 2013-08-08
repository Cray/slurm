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
#  include "config.h"
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/plugins/switch/cray/switch_cray.h"
#include "src/common/pack.h"
#include "src/plugins/switch/cray/alpscomm_cn.h"
#include "src/plugins/switch/cray/alpscomm_sn.h"

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

static void _print_jobinfo(slurm_cray_jobinfo_t *job)
{
	int i, j, rc;
	int32_t *nodes;

	xassert(job);
	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	debug("Address of slurm_cray_jobinfo_t structure: %p", job);
	debug("--Begin Jobinfo--");
	debug("  Magic: %" PRIx32, job->magic);
	debug("  num_cookies: %" PRIu32, job->num_cookies);
	debug("  --- cookies ---");
	for (i = 0; i < job->num_cookies; i++) {
		debug("  cookies[%d]: %s", i, job->cookies[i]);
	}
	debug("  --- cookie_ids ---");
	for (i = 0; i < job->num_cookies; i++) {
		debug("  cookie_ids[%d]: %" PRIu32, i, job->cookie_ids[i]);
	}
	debug("  ------");
	if (job->step_layout) {
		debug("  node_cnt: %" PRIu32, job->step_layout->node_cnt);
		debug("  node_list: %s", job->step_layout->node_list);
		debug("  --- tasks ---");
		for (i=0; i < job->step_layout->node_cnt; i++) {
			debug("  tasks[%d] = %u", i, job->step_layout->tasks[i]);
		}
		debug("  ------");
		debug("  task_cnt: %" PRIu32, job->step_layout->task_cnt);
		debug("  --- hosts to task---");
		rc = node_list_str_to_array(job->step_layout->node_cnt, job->step_layout->node_list, &nodes);
		if (rc) {
			error("(%s: %d: %s) node_list_str_to_array failed", THIS_FILE, __LINE__, __FUNCTION__);
		}
		for (i=0; i < job->step_layout->node_cnt; i++) {
			debug("Host: %d", i);
			for (j=0; j < job->step_layout->tasks[i]; j++) {
				debug("Task: %d", job->step_layout->tids[i][j]);
			}
		}
		debug("  ------");
	}
	debug("--END Jobinfo--");
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

	int i, rc;
	int num_cookies = 2;
	char *errMsg = NULL;
	char **cookies, **s_cookies;
	int32_t *nodes, *cookie_ids;
	uint32_t *s_cookie_ids;
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)switch_job;

	rc = node_list_str_to_array(step_layout->node_cnt, step_layout->node_list, &nodes);
	if (rc < 0) {
		error("(%s: %d: %s) node_list_str_to_array failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
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
				       "SLURM", 0,
				       0, nodes,
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
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_establish_GPU_mps_def_state: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
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
	s_cookie_ids = xmalloc(sizeof(uint32_t) * num_cookies);
	memcpy(s_cookie_ids, cookie_ids, sizeof(uint32_t) * num_cookies);
	free(cookie_ids);

	s_cookies = xmalloc(sizeof(char *) * num_cookies);
	memcpy(s_cookies, cookies, sizeof(char *) * num_cookies);
	for (i=0; i<num_cookies; i++) {
		s_cookies[i] = xstrdup(cookies[i]);
		free(cookies[i]);
	}
	free(cookies);

	if (rc) {
		if (errMsg) {
			error("(%s: %d: %s) Failed to obtain %d cookie%s: Errno: %d -- %s",
					THIS_FILE, __LINE__, __FUNCTION__, num_cookies,
					(num_cookies == 1) ? "" : "s", rc, errMsg);
		} else {
			error("(%s: %d: %s) Failed to obtain %d cookie%s: No error message present.  Errno: %d", THIS_FILE, __LINE__, __FUNCTION__, num_cookies,
					(num_cookies == 1) ? "" : "s", rc);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_establish_GPU_mps_def_state: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}

	/*
	 * Populate the switch_jobinfo_t struct
	 */
	job->num_cookies = num_cookies;
	job->cookies = s_cookies;
	job->cookie_ids = s_cookie_ids;
	job->step_layout = step_layout;

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
	xassert(switch_job);
	xassert(((slurm_cray_jobinfo_t *)switch_job)->magic == CRAY_JOBINFO_MAGIC);

	if (switch_p_alloc_jobinfo(&new_init, old->jobid, old->stepid)) {
		error("Allocating new jobinfo");
		slurm_seterrno(ENOMEM);
		return NULL;
	}

	new = (slurm_cray_jobinfo_t *)new_init;
	// Copy over non-malloced memory.
	*new = *old;

	new->cookies = xmalloc(old->num_cookies * sizeof(char *));
	for(i=0; i<old->num_cookies; i++) {
		new->cookies[i] = xstrdup(old->cookies[i]);
	}
	new->cookie_ids = xmalloc(old->num_cookies * sizeof(new->cookie_ids));
	memcpy(new->cookie_ids, old->cookie_ids, old->num_cookies * sizeof(uint32_t));

	new->step_layout = xmalloc(sizeof(slurm_step_layout_t));
	*(new->step_layout) = *(old->step_layout);
	new->step_layout->node_list = xstrdup(old->step_layout->node_list);
	new->step_layout->tasks = xmalloc(old->step_layout->node_cnt *
				sizeof(new->step_layout->tasks));
	memcpy(new->step_layout->tasks, old->step_layout->tasks,
			sizeof(*(old->step_layout->tasks)) * old->step_layout->node_cnt);
	new->step_layout->tids = xmalloc(sizeof(old->step_layout->tids) *
			old->step_layout->node_cnt);
	for (i=0; i<old->step_layout->node_cnt; i++) {
		new->step_layout->tids[i] = xmalloc(sizeof(*(*(old->step_layout->tids))) * old->step_layout->tasks[i]);
		memcpy(new->step_layout->tids[i], old->step_layout->tids[i], old->step_layout->tasks[i]);
	}

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

		// Free the individual cookie strings.
		for (i = 0; i < job->num_cookies; i++) {
			if(job->cookies[i]) {
				xfree(job->cookies[i]);
			}
		}
		// Free the cookie array
		if (job->cookies) {
			xfree(job->cookies);
		}
	}

	if(run_in_daemon("slurmd,slurmstepd")) {
		slurm_step_layout_destroy(job->step_layout);
	}

	xfree(job);

	return;
}

/*
 * pack_test1
 * Description:
 * Tests the packing by unpacking just the magic number.
 */
int pack_test1(Buf buffer) {
	int rc;
	uint32_t magic;
	rc = unpack32(&magic, buffer);
	debug("(%s:%d: %s) magic: %" PRIx32, THIS_FILE, __LINE__, __FUNCTION__, magic);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*
 * pack_test
 * Description:
 * Tests the packing by doing some unpacking.
 */
int pack_test(Buf buffer, uint32_t job_id, uint32_t step_id) {

	int rc;
	uint32_t num_cookies;
	slurm_cray_jobinfo_t *job;
	switch_p_alloc_jobinfo(&job, job_id, step_id);
	xassert(job);
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	xassert(buffer);
	rc = unpack32(&job->magic, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
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
	rc = unpack32_array(&(job->cookie_ids), &(job->num_cookies), buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32_array failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	/*
	 * Allocate our own step_layout function.
	 */
	rc = unpack_slurm_step_layout(&(job->step_layout), buffer, SLURM_PROTOCOL_VERSION);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	debug("(%s:%d: %s) switch_jobinfo_t contents:", THIS_FILE, __LINE__, __FUNCTION__);
	_print_jobinfo(job);

	return SLURM_SUCCESS;
}

/*
 * TODO: Pack job id, step id, and apid
 */
int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer)
{
	int i;
	int rc;
	uint32_t save_processed;

	slurm_cray_jobinfo_t *job= (slurm_cray_jobinfo_t *)switch_job;

	xassert(job);
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	xassert(buffer);

	/*Debug Example
	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH)
		debug("(%s:%d) job id: %u -- No nodes in bitmap of "
				"job_record!",
				THIS_FILE, __LINE__, __FUNCTION__, job_ptr->job_id);
	 */

	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		debug("(%s: %d: %s) switch_jobinfo_t contents", THIS_FILE, __LINE__, __FUNCTION__);
		_print_jobinfo(job);
	}

	save_processed = buffer->processed;
	pack32(job->magic, buffer);

	/*
	buffer->processed = save_processed;
	rc = pack_test1(buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) pack_test1 failed.",
				THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	*/

	/*
	pack32(job->num_cookies, buffer);
	packstr_array(job->cookies, job->num_cookies, buffer);
*/

	/*
	 *  Range Checking on cookie_ids
	 *  We're using a signed integer to store the cookies and a function that
	 *  packs unsigned uint32_t's, so I'm that the cookie_ids
	 *  are not negative so that they don't underflow the uint32_t.
	 */

	/*
	for (i=0; i < job->num_cookies; i++) {
		if (job->cookie_ids[i] < 0) {
			error("(%s: %d: %s) cookie_ids is negative.",
					THIS_FILE, __LINE__, __FUNCTION__);
			return SLURM_ERROR;
		}
	}
	pack32_array(job->cookie_ids, job->num_cookies, buffer);
	pack_slurm_step_layout(job->step_layout, buffer, SLURM_PROTOCOL_VERSION);
*/
	/*
	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
	*/
		/*
		 * We need to put the buffer pointer back to where it was before the packing
		 * process started if we're going to do a test unpack correctly.
		 */

		/*
		buffer->processed = save_processed;
		rc = pack_test(buffer, job->jobid, job->stepid);
		if (rc != SLURM_SUCCESS) {
			error("(%s: %d: %s) pack_test failed.",
					THIS_FILE, __LINE__, __FUNCTION__);
			return SLURM_ERROR;
		}
		*/
/*
}
*/
	return 0;
}

/*
 * TODO: Unpack job id, step id, and apid
 */

int switch_p_unpack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer)
{

	int rc;
	uint32_t num_cookies;
	/*
	char *DEBUG_WAIT=getenv("SLURM_DEBUG_WAIT");
	while(DEBUG_WAIT);
	*/

	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)switch_job;
	xassert(job);
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	xassert(buffer);

	rc = unpack32(&job->magic, buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}
	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	/*
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
	rc = unpack32_array(&(job->cookie_ids), &(job->num_cookies), buffer);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32_array failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

*/
	/*
	 * Allocate our own step_layout function.
	 */

	/*
	rc = unpack_slurm_step_layout(&(job->step_layout), buffer, SLURM_PROTOCOL_VERSION);
	if (rc != SLURM_SUCCESS) {
		error("(%s: %d: %s) unpack32 failed. Return code: %d", THIS_FILE,
				__LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

*/
	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		debug("(%s:%d: %s) switch_jobinfo_t contents:", THIS_FILE, __LINE__, __FUNCTION__);
		_print_jobinfo(job);
	}

	return SLURM_SUCCESS;
 unpack_error:
    error("Cray switch plugin: switch_p_unpack_jobinfo failed");
	return SLURM_ERROR;
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
	slurm_cray_jobinfo_t *sw_job = (slurm_cray_jobinfo_t *)job->switch_job;
	int rc, numPTags, cmdIndex, num_app_cpus, total_cpus, cpu_scaling, mem_scaling, i, j;
	uint32_t total_mem = 0;
	int *pTags;
	char *errMsg = NULL, *apid_dir;
	alpsc_peInfo_t alpsc_peInfo;
	FILE *f = NULL;
	size_t sz = 0;
	ssize_t lsz;
	char *lin = NULL;
	char meminfo_str[1024];
	int meminfo_value, gpu_enable = 0;
	hostlist_t hl;
	uint32_t task;
	int32_t *task_to_nodes_map;
	int32_t *nodes;
	gni_ntt_descriptor_t *ntt_desc_ptr = NULL;

	// Dummy variables to satisfy alpsc_write_placement_file
	int controlNid = 0, numBranches = 0;
	struct sockaddr_in controlSoc;
	alpsc_branchInfo_t alpsc_branchInfo;

	rc = job_attach_reservation(job->cont_id, job->jobid, 0);
	if (rc) {
		error("(%s: %d: %s) job_attach_reservation failed: %d", THIS_FILE, __LINE__, __FUNCTION__, errno);
		return SLURM_ERROR;
	}

	/*
	 * Create APID directory
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
	f = fopen("/sys/devices/system/cpu/online", "r");

	while (!feof(f)) {
		lsz = getline(&lin, &sz, f);
		if (lsz > 0) {
			hl = hostlist_create(lin);
			if (hl == NULL) {
				error("(%s: %d: %s) hostlist_create failed: %d", THIS_FILE,
						__LINE__, __FUNCTION__, errno);
				return SLURM_ERROR;
			}
			total_cpus += hostlist_count(hl);
			hostlist_destroy(hl);
			free(lin);
		}
	}
	fclose(f);

	//Use /proc/meminfo to get the total amount of memory on the node
	f = fopen("/proc/meminfo", "r");

	while (!feof(f)) {
		lsz = getline(&lin, &sz, f);
		sscanf(lin, "%s %d", meminfo_str, &meminfo_value);
		if(!strcmp(meminfo_str, "MemTotal:")) {
			total_mem = meminfo_value;
			break;
		}
		free(lin);
	}
	fclose(f);

	if (total_mem == 0) {
		error("(%s: %d: %s) Scanning /proc/meminfo results in MemTotal=0", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	// Scaling
	num_app_cpus = job->node_tasks * job->cpus_per_task;
	cpu_scaling = num_app_cpus / total_cpus;
	if (job->step_mem & MEM_PER_CPU) {
		/*
		 * This means that job->step_mem is a the amount of memory per CPU,
		 * not total.
		 */
		mem_scaling = (job->step_mem * num_app_cpus) / total_mem;

	} else {
		mem_scaling = job->step_mem / total_mem;
	}

	rc = alpsc_configure_nic(&errMsg, 0, cpu_scaling,
	    mem_scaling, job->cont_id, sw_job->num_cookies, (const char **)sw_job->cookies,
	    &numPTags, &pTags, ntt_desc_ptr);
	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_configure_nic failed: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_configure_nic failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_configure_nic: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
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
	rc = get_first_pe(job->nodeid, sw_job->step_layout->task_cnt,
			sw_job->step_layout->tids, &alpsc_peInfo.firstPeHere);
	if (rc < 0) {
		error("(%s: %d: %s) get_first_pe failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	/*
	 * Fill in alpsc_peInfo.peNidArray
	 *
	 * The peNidArray maps tasks to nodes.
	 * Basically, reverse the tids variable which maps nodes to tasks.
	 */
	rc = node_list_str_to_array(sw_job->step_layout->node_cnt,
			sw_job->step_layout->node_list, &nodes);
	if (rc < 0) {
		error("(%s: %d: %s) node_list_str_to_array failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	task_to_nodes_map = xmalloc(sw_job->step_layout->task_cnt * sizeof(int));

	for (i=0; i<sw_job->step_layout->node_cnt; i++) {
		for (j=0; j < sw_job->step_layout->tasks[i]; j++) {
			task = sw_job->step_layout->tids[i][j];
			task_to_nodes_map[task] = nodes[i];
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
		for (i = 0; i < alpsc_peInfo.totalPEs ; i++ ) {
			alpsc_peInfo.peCmdMapArray[i] = cmdIndex;
		}
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
	alpsc_branchInfo.tAddr = controlSoc; // Just assing controlSoc because it's already zero.
	alpsc_branchInfo.tIndex = 0;
	alpsc_branchInfo.tLen = 0;
	alpsc_branchInfo.targ = 0;

	alpsc_write_placement_file(&errMsg, sw_job->apid, cmdIndex, &alpsc_peInfo,
			controlNid, controlSoc, numBranches, &alpsc_branchInfo);
	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_write_placement_file failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_write_placement_file failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_write_placement_file: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}

	/*
	 * Query the generic resources to see if the GPU should be allocated
	 */

	alpsc_pre_launch_GPU_mps(&errMsg, gpu_enable);
	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_prelaunch_GPU_mps failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_prelaunch_GPU_mps failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_prelaunch_GPU_mps: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}


	/* Clean up */
	xfree(task_to_nodes_map);

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

extern void switch_p_job_suspend_info_pack(void *suspend_info, Buf buffer)
{
	return;
}

extern int switch_p_job_suspend_info_unpack(void **suspend_info, Buf buffer)
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
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *)jobinfo;
	int rc;
	char *apid_dir = NULL;
	rc = asprintf(&apid_dir, "/var/spool/alps/%" PRIu64, job->apid);
	if (rc == -1) {
		error("(%s: %d: %s) asprintf failed", THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

	// Stolen from ALPS
	recursiveRmdir(apid_dir);
	free(apid_dir);

	return SLURM_SUCCESS;
}

int switch_p_job_postfini(switch_jobinfo_t *jobinfo, uid_t pgid,
			   uint32_t job_id, uint32_t step_id)
{
	int rc;
	char *errMsg = NULL;

	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu",
		       (unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		debug("Job %u.%u: Bad pid value %lu", job_id,
		      step_id, (unsigned long) pgid);
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
	do_drop_caches();

	// Compact Memory
	/*
	alpsc_compact_mem(&errMsg, int numNodes, int *numaNodes,
	    cpu_set_t *cpuMasks, const char *cpusetDir);
	 */
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
				   Buf buffer)
{
	return 0;
}

extern int switch_p_unpack_node_info(switch_node_info_t *switch_node,
				     Buf buffer)
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

	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
		debug("(%s:%d: %s) switch_p_job_step_complete", THIS_FILE, __LINE__, __FUNCTION__);
	}

	/* Release the cookies */

	rc = alpsc_release_cookies(&errMsg, job->cookie_ids, job->num_cookies);

	if (rc != 0) {

		if (errMsg) {
			error("(%s: %d: %s) alpsc_release_cookies failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_release_cookies failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;

	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_release_cookies: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
		free(errMsg);
	}


	/*
	 * Inform the system that an application (i.e. job step) is terminating.
	 * This is for tracking purposes for congestion management and power
	 * management.
	 *
	 * TODO: Implement the actual call.
	 */
	// alpsc_put_app_end_info();

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
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_init(void)
{
	int rc = 0;
	char *errMsg = NULL;

	// Create the ALPS directories
	char dir[] = "/var/spool/alps/";
	char dir1[] = "/var/opt/cray/alps/";


	rc = mkdir_safe(dir, 755);
	if (rc) {
		error("(%s: %d: %s) mkdir %s failed: %s", THIS_FILE, __LINE__, __FUNCTION__, dir,
				strerror(errno));
		return SLURM_ERROR;
	}

	// Create the directory
	rc = mkdir_safe(dir1, 755);
	if (rc) {
		error("(%s: %d: %s) mkdir %s failed: %s", THIS_FILE, __LINE__, __FUNCTION__, dir,
				strerror(errno));
		return SLURM_ERROR;
	}

	// Create the symlink to the real directory
	rc = symlink(dir, "/var/opt/cray/alps/spool");
	if (!rc) {
		error("(%s: %d: %s) Failed to create symlink /var/opt/cray/alps/spool "
				"-> %s", THIS_FILE, __LINE__, __FUNCTION__, dir);
		return SLURM_ERROR;
	}

	// Establish GPU's default state
	rc = alpsc_establish_GPU_mps_def_state(&errMsg);
	if (rc != 1) {
		if (errMsg) {
			error("(%s: %d: %s) alpsc_establish_GPU_mps_def_state failed: %s",
					THIS_FILE, __LINE__, __FUNCTION__, errMsg);
			free(errMsg);
		}
		else {
			error("(%s: %d: %s) alpsc_establish_GPU_mps_def_state failed: No error message present.", THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (errMsg) {
		info("(%s: %d: %s) alpsc_establish_GPU_mps_def_state: %s", THIS_FILE, __LINE__, __FUNCTION__, errMsg);
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
 * Function: node_list_str_to_array
 * Description:
 * 	Convert the node_list string into an array of nid integers.
 *
 * IN node_cnt  -- The number of nodes in the node list string
 * IN node_list -- The node_list string
 * OUT nodes    -- Array of node_cnt nids;  Caller is responsible to xfree()
 *                 this.
 *
 * RETURNS
 * Returns 0 on success and -1 on failure.
 */

static int node_list_str_to_array(uint32_t node_cnt, char *node_list,
		int32_t **nodes) {

	int32_t *nodes_ptr = NULL;
	hostlist_t hl;
	int i, ret = 0;
	char *node_str, *cptr;

	/*
	 * Create a hostlist
	 */
	if ((hl = hostlist_create(node_list)) == NULL) {
		error("hostlist_create error on %s",
				node_list);
		return ESLURM_INVALID_NODE_NAME;
	}

	/*
	 * Create an integer array of nodes_ptr in the same order as in the node_list.
	 */
	nodes_ptr = *nodes = xmalloc(node_cnt * sizeof(uint32_t));
	if (nodes_ptr == NULL) {
		error("(%s: %d: %s) xmalloc failed", THIS_FILE, __LINE__, __FUNCTION__);
		hostlist_destroy(hl);
		return -1;
	}
	for (i = 0; i < node_cnt; i++) {
		// node_str must be freed using free(), not xfree()
		node_str = hostlist_shift(hl);
		if (node_str == NULL) {
			error("(%s: %d: %s) hostlist_shift error", THIS_FILE, __LINE__, __FUNCTION__);
			xfree(nodes_ptr);
			hostlist_destroy(hl);
			return -1;
		}
		cptr = strpbrk(node_str, "0123456789");
		if (cptr == NULL) {
			error("(%s: %d: %s) Error: Node was not recognizable: %s", THIS_FILE,
					__LINE__, __FUNCTION__, node_str);
			xfree(nodes_ptr);
			hostlist_destroy(hl);
		}
		nodes_ptr[i] = atoll(cptr);
		free(node_str);
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
		fnm = xmalloc(fnmLen);
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
				error("Error removing %s", fnm);
			}
		}
	}
	xfree(fnm);
	closedir(dirp);
	fileDel:
	st = unlink(dirnm);
	if (st < 0 && errno == EISDIR) st = rmdir(dirnm);
	if (st < 0 && errno != ENOENT) {
		error("Error removing %s", dirnm);
	}
}

/*
 * Function: mkdir_safe
 * Description:
 *   Create the directory safely.
 *
 * IN dir -- path to the directory
 * IN flags -- flags
 *
 * RETURNS
 * On success return 0, on error return -1
 */

static int mkdir_safe(const char *pathname, mode_t mode) {
	int rc, errnum;
	struct stat buf;

	rc = mkdir(pathname, mode);
	errnum = errno;
	if (rc) {
		if(errno == EEXIST) {
			// Check that the path really is a directory.
			rc = stat(pathname, &buf);
			if (rc) {
				error("(%s: %d: %s) stat on %s failed: %s", THIS_FILE, __LINE__, __FUNCTION__, pathname,
						strerror(errno));
				errno = errnum;
				return -1;
			}
			if(!S_ISDIR(buf.st_mode)) {
				error("(%s: %d: %s) mkpathname %s failed: %s", THIS_FILE, __LINE__, __FUNCTION__, pathname,
						strerror(errnum));
				return -1;
			}
		} else {
			error("(%s: %d: %s) mkpathname %s failed: %s", THIS_FILE, __LINE__, __FUNCTION__, pathname,
					strerror(errno));
			return -1;
		}
	}

	return 0;
}

/*
 * Flush the kernel's VM caches.  This is equivalent to:
 *
 *      echo 3 > /proc/sys/vm/drop_caches
 *
 */
static void
do_drop_caches(void)
{
    int fd;
    ssize_t n;

    fd = open("/proc/sys/vm/drop_caches", O_WRONLY, 0);
    if (fd < 0) {
        error("(%s: %d: %s): open: errno %d, %s", THIS_FILE, __LINE__, __FUNCTION__, errno,
        		strerror(errno));
        return;
    }
    debug("(%s: %d: %s) writing 3 to /proc/sys/vm/drop_caches", THIS_FILE, __LINE__, __FUNCTION__);
    n = write(fd, "3\n", 2);
    if (n != 2) {
    	error("(%s: %d: %s): write: wrote %zd of 2, errno %d, %s", THIS_FILE,
    			__LINE__, __FUNCTION__, n, errno, strerror(errno));
    }
    close(fd);
}
