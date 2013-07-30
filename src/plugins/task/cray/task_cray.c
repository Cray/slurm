/*****************************************************************************\
 *  task_cray.c - Library for task pre-launch and post_termination functions
 *	on a cray system
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
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

#include <signal.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

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
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "task CRAY plugin";
const char plugin_type[]        = "task/cray";
const uint32_t plugin_version   = 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini (void)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_batch_request()
 */
extern int task_p_slurmd_batch_request (uint32_t job_id,
					batch_job_launch_msg_t *req)
{
	debug("task_p_slurmd_batch_request: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_launch_request()
 */
extern int task_p_slurmd_launch_request (uint32_t job_id,
					 launch_tasks_request_msg_t *req,
					 uint32_t node_id)
{
	debug("task_p_slurmd_launch_request: %u.%u %u",
	      job_id, req->job_step_id, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_reserve_resources()
 */
extern int task_p_slurmd_reserve_resources (uint32_t job_id,
					    launch_tasks_request_msg_t *req,
					    uint32_t node_id)
{
	debug("task_p_slurmd_reserve_resources: %u %u", job_id, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_suspend_job()
 */
extern int task_p_slurmd_suspend_job (uint32_t job_id)
{
	debug("task_p_slurmd_suspend_job: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_resume_job()
 */
extern int task_p_slurmd_resume_job (uint32_t job_id)
{
	debug("task_p_slurmd_resume_job: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_release_resources()
 */
extern int task_p_slurmd_release_resources (uint32_t job_id)
{
	debug("task_p_slurmd_release_resources: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_setuid() is called before setting the UID for the
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
extern int task_p_pre_setuid (stepd_step_rec_t *job)
{
	int i, j, rc;
	uint16_t multi_prog = 0;  // Is this an MPMD or SPMD launch? 1 = MPMD 0 = SPMD
	/* Layout variables */
	slurm_step_layout_t *layout;
	uint32_t step_node_count, step_task_count, task;
	uint16_t *tasks_per_node;
	uint32_t **host_to_task_map;
	uint32_t stepid, cpus_per_task;
	uint64_t cpuMaskHere; // Really a type cpuMap_t, but that's an ALPS internal
	uint32_t pesHere, totalPEs;
	uint32_t numNodes;
	uint64_t apid, nid;

	apInfo_t ap;
	char *cptr = NULL, *cptr1 = NULL;

//	char *sleep_time = NULL;
//	int seconds = 60; // Default to a minute
	FILE *fout;

	char *DEBUG_WAIT=getenv("SLURM_DEBUG_WAIT");
	while(DEBUG_WAIT);

	fout = fopen("/tmp/slurmstepd_output", "w");

	fprintf(fout, "Put something in here.\n");

	fsync(fileno(fout));

	/*
	sleep_time = getenv("SLURM_SLEEP_TIMER");
	if (sleep_time) {
		seconds = atoi(sleep_time);
	}

	// Sleep long enough to attach GDB to the function.

	sleep(seconds);
	 */

	/*
	 * Get SLURM information
	 * For now, I'm just translating SLURM-named variables into Cray-named
	 * variables.
	 */

	numNodes = job->nnodes;
	totalPEs = job->ntasks;
	pesHere = job->node_tasks;
	layout = job->layout;
	step_node_count = layout->node_cnt;
	step_task_count = layout->task_cnt;
	tasks_per_node = layout->tasks;
	host_to_task_map = layout->tids;
	/* There may be problems here because I think job_alloc_cores only goes up
	 * to size uint32_t, not uint64_t.
	 */
	/*
	 *
	 rc = bit_unfmt((bitstr_t *)&cpuMaskHere, job->job_alloc_cores); // CPU/core bit map
	if (rc < 0 ) {
		error("Failed to set cpuMaskHere using bit_unfmt");
		return SLURM_ERROR;
	}
	*/

	cpus_per_task = job->cpus_per_task;

	/* JDS: To incorporate both the JOB ID and the STEPID, I bit shifted the
		 * the jobid into the upper 32 bits.
		 */
	apid = make_apid(job->jobid, job->stepid);

	/* Create APID directory */
	/* TO DO: rmdir() this directory in the clean-up section. */
	rc = make_apid_dir(apid, job->uid, job->gid);
	if (rc) {
		fprintf(stderr, "Making APID directory failed: APID: %llu.", apid);
	}

	/*
	 * Configure the Cray network
	 */

	fprintf(fout, "Configure_network\n");
	configure_network(apid, job->uid, cookie_array);

	// For testing, inc the cookie_array cookies for the next job step.
	//cookie_array[0] += 0x020000;
	//cookie_array[1] += 0x020000;

	/*
	 * Write information into the ALPS placement file for Cray's PMI layer.
	 * First, populate the ALPS information into ap.
	 * The values depend on whether this job launch is an MPMD launch or an
	 * SPMD launch.  Determine that first before proceeding.
	 */

	/* Launch independent */
	ap.apid = apid;
	// I think I converted the cupMaskHere to a uint64_t correctly.
	ap.cpuMaskHere = cpuMaskHere;
	/* I'm not sure that this is a one-to-one match, and it may need to change.*/
	/* This one is not needed by Cray's PMI. */
	ap.peDepth = cpus_per_task;

	/*
	 * peNidArray
	 * This takes the SLURM ordering within a 2-D array that maps hosts to
	 * their associated tasks and flattens it into a 1-D array mapping
	 * tasks to hosts.  In Cray parlance, it maps PEs to nodes.
	 */
	fprintf(fout, "host_to_task_map configuring\n");
	ap.peNidArray = calloc(step_task_count, sizeof(int));
	for (i=0; i<step_node_count; i++) {
		for (j=0; j<tasks_per_node[i]; j++) {
			task = host_to_task_map[i][j];
			if (task > step_task_count - 1) {
				fprintf(stderr, "ERROR: Task number %ul exceeds bounds of task "
						"array: Max %ul", task, step_task_count - 1);
				return(1);
			}
			cptr = strpbrk(slurm_step_layout_host_name(layout,task), "0123456789");
			nid = atoi(cptr);
			ap.peNidArray[task] = nid;
			debug("task_pre_setuid: %llu, %llu", nid, task);
		}
	}

	// Does this need to be SPMD/MPMD specific?
	ap.totalPEs = totalPEs;


	if (multi_prog) {
		/* MPMD Launch */
		/*
		 * Need to fill in this information.  I may need to parse the MPMD
		 * configuration file to get it.
		 */
		/*
			ap.cmdIndex = ;
			ap.peCmdMapArray = ;
			ap.firstPeHere = ;

			ap.pesHere = pesHere; // These need to be MPMD specific.
		 */

	} else {
		/* SPMD Launch */
		ap.cmdIndex = 0;

		ap.peCmdMapArray = calloc(totalPEs, sizeof(int));
		for (i = 0; i < totalPEs; i++ ) {
			ap.peCmdMapArray[i] = ap.cmdIndex;
		}

		ap.firstPeHere = get_first_pe(job->nodeid, pesHere, host_to_task_map);

		ap.pesHere = pesHere;

	}

	/*
	 * I'm not sure whether I need to set the branches or not.
	 * For now, I'm copping out and setting the first one invalid.
	 * This isn't right, but we'll sort it out later.
	 * TO DO: I should only have the first process, local 0, on the node
	 * write this file.
	 */
	ap.branch[0].targFd = -1;

	/*
	 * Attempt to add the cookie variables to the task's environment
	 * This is rather hacky and should be refined.
	 */
    cptr = getenv("CRAY_NUM_COOKIES");
    if (!cptr) {
	DEBUGP("%s: ERROR failed to find env var CRAY_NUM_COOKIES\n", __func__);
	return 1;
    }

    cptr1 = getenv("CRAY_COOKIES");
    if (!cptr1) {
	DEBUGP("%s: ERROR failed to find env var CRAY_COOKIES\n", __func__);
	return 1;
    }

	env_array_overwrite_fmt(&(job->env),"CRAY_NUM_COOKIES", "%s",
							    cptr);
	env_array_overwrite_fmt(&(job->env),"CRAY_COOKIES", "%s",
					    cptr1);


	fprintf(fout, "WritePlacementFile\n");
	writePlacementFile(&ap);

	if (cookie_array) {
		free(cookie_array);
	}
	fclose(fout);

	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
extern int task_p_pre_launch (stepd_step_rec_t *job)
{
	debug("task_p_pre_launch: %u.%u, task %d",
	      job->jobid, job->stepid, job->envtp->procid);
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch_priv() is called prior to exec of application task.
 * in privileged mode, just after slurm_spank_task_init_privileged
 */
extern int task_p_pre_launch_priv (stepd_step_rec_t *job)
{
	char *ptr;
	int rc;

	debug("task_pre_launch_priv: %u.%u",
		job->jobid, job->stepid);

	/*
	 * Send the rank to the application's PMI layer via an environment variable.
	 */
	rc = send_rank_to_app(job->envtp->procid);

	if (rc) {
		// Should reword this error because I'm peering behind the abstraction barrier here.
		debug("Failed to set env variable ALPS_APP_PE");
		return SLURM_ERROR;
	}

	/*
	 * Send the rank to the application's PMI layer via an environment variable.
	 */
	rc = turn_off_pmi_fork();

	if (rc) {
		// Should reword this error because I'm peering behind the abstraction barrier here.
		debug("Failed to set env variable PMI_NO_FORK");
		return SLURM_ERROR;
	}

	// Debug stuff
	if (slurm_get_debug_flags() & DEBUG_FLAG_SWITCH) {
	  debug("(%s:%d) task_p_pre_launch_priv: %u.%u ALPS_APP_PE (i.e. rank): "
		"%s", THIS_FILE, __LINE__, job->jobid, job->stepid, getenv("PMI_NO_FORK"));
	}

	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_p_post_term (stepd_step_rec_t *job, stepd_step_task_info_t *task)
{
	debug("task_p_post_term: %u.%u, task %d",
	      job->jobid, job->stepid, job->envtp->procid);
	return SLURM_SUCCESS;
}

/*
 * task_p_post_step() is called after termination of the step
 * (all the task)
 */
extern int task_p_post_step (stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}
