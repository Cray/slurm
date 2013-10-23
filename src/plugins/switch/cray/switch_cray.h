/*****************************************************************************\
 *  switch_cray.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2013 Cray
 *  Written by Jason Sollom <jasons@cray.com>
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
	uint32_t port;/* Port for PMI Communications */
	uint32_t       jobid;  /* Current SLURM job id */
	uint32_t       stepid; /* Current step id */
	/* Cray Application ID -- A unique combination of the job id and step id*/
	uint64_t apid;
	slurm_step_layout_t *step_layout;
};

static void _print_jobinfo(slurm_cray_jobinfo_t *job);
static int _get_first_pe(uint32_t nodeid, uint32_t task_count,
		uint32_t **host_to_task_map, int32_t *first_pe);
static int _list_str_to_array(char *list, int *cnt, int32_t **numbers);
static void _recursiveRmdir(const char *dirnm);
static int _get_cpu_total(void);
static int _init_port();
static int _assign_port(uint32_t *ret_port);
static int _release_port(uint32_t real_port);
static int _get_numa_nodes(char *path, int *cnt, int **numa_array);
static int _get_cpu_masks(char *path, cpu_set_t **cpuMasks);

#endif /* SWITCH_CRAY_H_ */
