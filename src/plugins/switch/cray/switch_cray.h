/*
 * switch_cray.h
 *
 *  Created on: Mar 29, 2013
 *      Author: sollom
 */

#ifndef SWITCH_CRAY_H_
#define SWITCH_CRAY_H_

#include "slurm/slurm.h"
#include <sys/stat.h>
#include <sys/types.h>

#define CRAY_JOBINFO_MAGIC	0xCAFECAFE

/* opaque data structures - no peeking! */
typedef struct slurm_cray_jobinfo  slurm_cray_jobinfo_t;

struct slurm_cray_jobinfo {
	uint32_t magic;
	uint32_t num_cookies;	/* The number of cookies sent to configure the HSN */
	/* Double pointer to an array of cookie strings.
	 * cookie values here as NULL-terminated strings.
	 * There are num_cookies elements in the array.
	 * The caller is responsible for free()ing
	 * the array contents and the array itself.  */
	char     **cookies;
	/* The array itself must be free()d when this struct is destroyed. */
	uint32_t *cookie_ids;
	uint32_t       jobid;  /* Current SLURM job id */
	uint32_t       stepid; /* Current step id */
	/* Cray Application ID -- A unique combination of the job id and step id*/
	uint64_t apid;
	slurm_step_layout_t *step_layout;
};

static void _print_jobinfo(slurm_cray_jobinfo_t *job);
static int get_first_pe(uint32_t nodeid, uint32_t task_count,
		uint32_t **host_to_task_map, int32_t *first_pe);
static int node_list_str_to_array(uint32_t node_cnt, char *node_list, int32_t **nodes);
static void recursiveRmdir(const char *dirnm);
static void do_drop_caches(void);
static int mkdir_safe(const char *pathname, mode_t mode);
static int get_cpu_total(void);

#endif /* SWITCH_CRAY_H_ */
