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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/param.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

// TODO: can we get this from alps.h?
#define LLI_STATUS_FILE	    "/var/opt/cray/alps/spool/status%d"
#define LLI_STATUS_FILE_ENV "ALPS_LLI_STATUS_FILE"

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
	verbose("%s loaded, really, really loaded.", plugin_name);
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
	debug("task_p_pre_setuid: %u.%u",
		job->jobid, job->stepid);

	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
extern int task_p_pre_launch (stepd_step_rec_t *job)
{
	int rc = 0;
	char buff[1024];

	debug("task_p_pre_launch: %u.%u, task %d",
	      job->jobid, job->stepid, job->envtp->procid);


	/*
	 * Send the rank to the application's PMI layer via an environment variable.
	 */
	snprintf(buff, sizeof(buff), "%d", job->envtp->procid);
	rc = env_array_overwrite(&job->env,"ALPS_APP_PE", buff);

	if (rc == 0) {
		debug("Failed to set env variable ALPS_APP_PE");
		return SLURM_ERROR;
	}

	/*
	 * Send the rank to the application's PMI layer via an environment variable.
	 */
	rc = env_array_overwrite(&job->env,"PMI_NO_FORK", "1");

	if (rc == 0) {
		debug("Failed to set env variable ALPS_APP_PE");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch_priv() is called prior to exec of application task.
 * in privileged mode, just after slurm_spank_task_init_privileged
 */
extern int task_p_pre_launch_priv (stepd_step_rec_t *job)
{
	char *llifile = NULL;
	int rv;
	
	debug("task_p_pre_launch_priv: %u.%u",
	      job->jobid, job->stepid);
	
	// Get the lli file name 
	rv = asprintf(&llifile, LLI_STATUS_FILE, job->envtp->procid);
	if (rv == -1) {
	    debug("%s: asprintf failed", __func__);
	    return SLURM_ERROR;
	}

	// Make the file
	rv = creat(llifile, 0644);
	if (rv == -1) {
	    debug("%s: creat(%s) failed: %m", __func__, llifile);
	    free(llifile);
	    return SLURM_ERROR;
	}

	// Change owner/group so app can write to it
	rv = chown(llifile, job->uid, job->gid);
	if (rv == -1) {
	    debug("%s: chown(%s) failed: %m", __func__, llifile);
	    free(llifile);
	    return SLURM_ERROR;
	}

	// Send the filename to the application
	rv = env_array_overwrite(&job->env, LLI_STATUS_FILE_ENV, llifile);
	if (rv == 0) {
	    debug("%s: Failed to set %s environment variable", 
		    __func__, LLI_STATUS_FILE_ENV);
	    free(llifile);
	    return SLURM_ERROR;
	}
	info("Set %s to %s", LLI_STATUS_FILE_ENV, llifile);

	free(llifile);
	
	return SLURM_SUCCESS;
}

/*
 * match_line() - determine whether the next line in fp matches
 *      the given string. Returns 0 on a match, 1 on mismatch,
 *      and 2 on getline failure 
 */
static int match_line (FILE *fp, const char *filename, const char *match, 
	char **line, size_t *linesiz)
{
	int rv;

	// Read the file
	rv = getline(line, linesiz, fp);
	if (rv == -1) {
		return 2;
	}

	if (strcmp(*line, match)) {
		debug("%s: %s line %s doesn't match %s", 
			__func__, filename, *line, match);
		return 1;
	}
	return 0;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_p_post_term (stepd_step_rec_t *job, stepd_step_task_info_t *task)
{
	char *llifile = NULL, *line = NULL;
	size_t linesiz = 0;
	int rv;
	FILE *fp;

	debug("task_p_post_term: %u.%u, task %d",
	      job->jobid, job->stepid, job->envtp->procid);
	
	// Get the lli file name 
	rv = asprintf(&llifile, LLI_STATUS_FILE, job->envtp->procid);
	if (rv == -1) {
	    debug("%s: asprintf failed", __func__);
	    return SLURM_ERROR;
	}

	// Open the lli file.
	errno = 0;
	fp = fopen(llifile, "r");
	if (fp == NULL) {
		debug("%s: fopen(%s) failed: %m", __func__, llifile);
		free(llifile);
		return SLURM_ERROR;
	}

	// No matter what happens from here on, we want to unlink the file
	rv = unlink(llifile);
	if (rv == -1) {
		debug("%s: unlink(%s) failed: %m", __func__, llifile);
		// Continue on anyway
	}

	// Read the lli file
	rv = match_line(fp, llifile, "starting\n", &line, &linesiz);
	
	// No starting message found, probably not an MPI app
	if (rv == 2 || line == NULL || strlen(line) == 0) {
		free(line);
		free(llifile);
		TEMP_FAILURE_RETRY(fclose(fp));
		return SLURM_SUCCESS;	
	} else if (rv == 1) {
		debug("%s: %s no starting message found", __func__, llifile);
		free(line);
		free(llifile);
		TEMP_FAILURE_RETRY(fclose(fp));
		return SLURM_ERROR;
	}

	rv = match_line(fp, llifile, "exiting\n", &line, &linesiz);
	if (rv) {
		free(line);
		free(llifile);
		TEMP_FAILURE_RETRY(fclose(fp));

		// Cancel the job step, since we didn't find the exiting msg
		debug("Terminating job step, task %d improper exit", 
			job->envtp->procid);
		slurm_terminate_job_step(job->jobid, job->stepid);
		return SLURM_ERROR;
	}

	free(line);
	free(llifile);
	TEMP_FAILURE_RETRY(fclose(fp));
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
