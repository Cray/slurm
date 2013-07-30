/*
 * (c) 2013 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

#ifndef __ALPSCOMM_CN_H
#define __ALPSCOMM_CN_H

#ident "$Id: alpscomm_cn.h 8195 2013-07-15 19:43:29Z kohnke $"

#include <stdint.h>
#include <sched.h>
/*
 * Commenting this out as it is not found.
 */
// #include <gni_pub.h>

/*
 * libalpscomm_cn - an external library interface for compute node
 * services which are common to both ALPS and native workload managers.
 */

/* ***************************************************************
 *                    Memory Cleanup APIs
 * ***************************************************************/

/*
 * alpsc_flush_lustre - clear the lustre buffers to reduce memory
 * fragmentation and allow more huge pages to be formed.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_flush_lustre(char **errMsg);

/*
 * alpsc_compact_mem - initiate memory compaction to reduce memory
 * fragmentation and allow more huge pages to be formed.  A child
 * is forked per requested NUMA node id and has its affinity set to
 * the CPUs within that NUMA node.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   numNodes - number of entries in the numaNodes array.
 *   numaNodes - array of NUMA node ids whose memory is to be compacted.
 *   cpuMasks - array of cpu_set_t cpumask per NUMA node id within the
 *              numaNodes array; the cpumask identifies which CPUs are
 *              within that NUMA node.
 *
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_compact_mem(char **errMsg, int numNodes, int *numaNodes,
    cpu_set_t *cpuMasks, const char *cpusetDir);


/* ***************************************************************
 *                    Compute Node Cleanup APIs 
 * ***************************************************************/

/*
 * alpsc_create_cncu_container - create a compute node kernel container
 * to track certain application related memory objects for all applications
 * within a batch job or interactive resource allocation.  This container
 * is used for compute node cleanup.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   cncuId - the batch job or reservation numeric identifier
 *
 * Returns
 *    1 if successful, else -1 for an error
 */ 
extern int alpsc_create_cncu_container(char **errMsg, uint64_t cncuId);

/*
 * alpsc_attach_cncu_container - attach an application local PAGG job
 * container to a compute node kernel container for use with compute
 * node cleanup.  Certain memory objects for the application within the 
 * local PAGG job container will be tracked and that information retained
 * after the local PAGG job container is removed following the
 * application exit.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   cncuId - the batch job or reservation numeric identifier
 *   paggLocal - the local PAGG job container id for the application
 *
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_attach_cncu_container(char **errMsg, uint64_t cncuId,
    uint64_t paggLocal);

/*
 * alpsc_cleanup_cncu_container - ask the kernel to remove the files and
 * other memory objects tracked within the compute node kernel container
 * as part of compute node cleanup following the exit of a batch job
 * or interactive resource allocation.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   cncuId - the batch job or reservation numeric identifier
 *
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_cleanup_cncu_container(char **errMsg, uint64_t cncuId);


/* ***************************************************************
 *                        NVIDIA GPU APIs 
 * ***************************************************************/

/*
 * alpsc_establish_GPU_mps_def_state - determine the default state of
 * the GPU proxy.  This only needs to be called once during compute node
 * daemon startup.
 * 
 * Arguments
 *    errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_establish_GPU_mps_def_state(char **errMsg);

/*
 * alpsc_pre_launch_GPU_mps - handle application pre-launch activities
 * related to the NVIDIA GPU to allow more than one process within the
 * application to schedule work on the GPU.  The GPU access can be enabled
 * or disabled through this call.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   enable - if non-zero, enable the GPU multi-rank access; if 0, 
 *             disable the GPU access.
 *
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_pre_launch_GPU_mps(char **errMsg, int enable);

/*
 * alpsc_post_launch_GPU_mps - handle application exit activities
 * related to the NVIDIA GPU to restore the GPU state to its default
 * state.  The enable argument should be the same value as provided
 * with the alpsc_pre_launch_GPU_mps call.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   enable - the same enable or disable value used with the
 *             alpsc_pre_launch_GPU_mps call
 *
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_post_launch_GPU_mps(char **errMsg, int enable);


/* ***************************************************************
 *                    Power Management APIs 
 * ***************************************************************/

/*
 * This structure holds the list of a node's performance governors
 * (strings) and allowable performance states (integers)
 */
typedef struct {
    char      *pgovernorCurrent; /* current performance governor */
    char      *pgovernorDefault; /* default performance governor */
    char     **pgovernorAll;     /* array of allowable performance governors */
    int        pgovernorNum;     /* number of performance governors in array */
    uint32_t   pstateCurrent;    /* current performance state (kHz) */
    uint32_t   pstateDefault;    /* default performance state (kHz) */
    uint32_t  *pstateAll;        /* array of allowable p-states (kHz) */
    int        pstateNum;        /* number of p-states in array */
} alpsc_powerInfo_t;


/*
 * alpsc_get_power_info - returns compute node power information
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   pinfo -  the provided pinfo structure is filled in with compute
 *            node power information.  The caller is responsible to
 *            free the memory allocated within this structure by calling
 *            alpsc_free_power_info() after the pinfo structure is no longer
 *            needed.
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_get_power_info(char **errMsg, alpsc_powerInfo_t *pinfo);

/*
 * alpsc_set_power_info - sets the provided power values for a performance
 * governor and/or performance state.  Updates the provided pinfo
 * structure pgovernorCurrent and pstateCurrent values, as applicable.
 * As needed, the pstate value is adjusted to the closest supported frequency.
 * The pgovernor value must match a supported performance governor on
 * the node.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   pinfo -  the provided pinfo structure is updated with the requested
 *            power setting after the node is configured with those values
 *   numCpus - number of CPUs on the node
 *   pstate - requested pstate value to be set
 *   pgovernor - requested governor value
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_set_power_info(char **errMsg, int numCpus, uint32_t pstate,
    const char *pgovernor, alpsc_powerInfo_t *pinfo);

/*
 * alpsc_free_power_info - frees any malloc'd space within the provided
 * pinfo fields and then reinitializes all of the pinfo fields to zero.
 * The pinfo structure itself is not freed.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   pinfo -  the provided pinfo structure field memory is freed and
 *            all fields reinitialized to zero.
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_free_power_info(char **errMsg, alpsc_powerInfo_t *pinfo);


/* ***************************************************************
 *                   Network Configuration APIs
 * ***************************************************************/

/*
 * This is a hack until this problem is resolved.
 */
struct gni_ntt_descriptor_s;
typedef struct gni_ntt_descriptor_s gni_ntt_descriptor_t;

/*
 * alpsc_configure_nic - configures the network driver, which includes
 * calculating and setting network resource limits.  The caller provides
 * scaling information to be used with dividing up network resources for
 * shared access of the node.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   exclusive - set to a non-zero value if the application is to have
 *            exclusive access to the network resources and the node
 *            will not be shared with another application (including
 *            suspend/resume activities).
 *   scaling - a whole number percent of the default resource limit for
 *            non-memory related network resources to be set for the
 *            application.  Valid values are 1 to 100.
 *   scalingMem - a whole number percent of the default resource limit for
 *            memory related network resources to be set for the application.
 *            Valid values are 1 to 100.
 *   pagg -   the local PAGG job id for the container within which the
 *            application is executing.
 *   numCookies - the number of cookies within the cookies array.
 *   cookies - an array of cookies (which include an embedded pKey) assigned
 *             to the application.
 *   numPTags - will be set to the returned number of protection tag (pTag)
 *             entries within the pTags array
 *   pTags -   an array of protection tags which will be returned with the
 *             network driver assigned pTags, one per cookie.
 *   ntt_desc_ptr - pointer to a structure containing NTT information,
 *             which should be NULL within a native workload manager
 *             environment.
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_configure_nic(char **errMsg, int exclusive, int scaling,
    int scalingMem, uint64_t pagg, int numCookies, const char *cookies[],
    int *numPTags, int **pTags, gni_ntt_descriptor_t *ntt_desc_ptr);


/* ***************************************************************
 *                   Misc APIs
 * ***************************************************************/

/*
 * alpsc_get_abort_info - uses the provided PAGG identifier to retrieve
 * any abort messages written by certain system components which apply
 * to the application which executed within that PAGG job container.
 * Examples are out-of-memory killer, DVS and network driver actions
 * which resulted in the application being killed.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   pagg - the PAGG identifier to use to find abort information
 *   abortInfo - returned abort information, as applicable, else
 *               null; the caller is responsible to free the memory
 *               allocated for a message.
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_get_abort_info(char **errMsg, uint64_t pagg,
    char **abortInfo);

/*
 * This structure holds processing element (PE) (i.e. rank) information for
 * the application.  The peNidArray has an entry for each PE, which is the
 * assigned compute node id (nid) for that PE. The peCmdMapArray also has
 * an entry for each PE, which is the MPMD command index applicable for
 * that PE.  The nodeCpuArray has an entry for each assigned compute node id,
 * which is the number of assigned CPUs on that node.  The number of
 * CPUs is the number of local PEs and PE threads assigned on that
 * compute node.
 */
typedef struct {
    int totalPEs;    /* Total number of PEs for this application */
    int firstPeHere; /* PE number of the first PE on this node */
    int pesHere;     /* Number of PEs on this node */
    int peDepth;     /* Number of threads per PE on this node, one per CPU */
    int *peNidArray; /* All assigned nids, one entry per PE */
    int *peCmdMapArray;  /* MPMD command numbers, one entry per PE */
    int *nodeCpuArray;   /* Assigned number of CPUs, one entry per node  */
} alpsc_peInfo_t;

/*
 * This structure holds control tree fanout information. There is an
 * entry for each target branch compute node child controlled by the 
 * the parent compute node.  A compute node which is a leaf within a
 * fanout control tree will not have any controlled target branch nodes.
 */
typedef struct {
    int targ;   /* The nid of a controlled target branch node */
    int tIndex; /* Placement list start index (first entry) for this nid */
    int tLen;   /* Placement list length (number of entries) for this target */
    struct sockaddr_in tAddr; /* IP address for this target node */
} alpsc_branchInfo_t;

/*
 * alpsc_write_placement_file - creates and writes a compute node placement
 * file which contains information about the application placement.  This
 * file contains information specific to a compute node and also total
 * placement related information.  The information within this file can
 * be accessed through the libalpsutil alps_get_placement_info() procedure.
 *
 * Arguments
 *   errMsg - returns a fatal error message or info message depending
 *            upon the procedure return value; the caller is responsible
 *            to free the memory allocated for a message.
 *   apid - the application identifier.
 *   cmdIndex - the MPMD command index for the application MPMD element
 *              to execute on this compute node.
 *   alpsc_peInfo - Information about the PEs in total and locally on a
 *                  single compute node.
 *   controlNid - the compute node id (nid) of this node's parent node
 *   controlSocket - IP address for this node's parent node.
 *   numBranches - the number of target branches within the alpsc_branchInfo
 *                 array.
 *   alpsc_branchInfo - an array of information about each branch target
 *                 compute node controlled by this parent node.
 * Returns
 *    1 if successful, else -1 for an error
 */
extern int alpsc_write_placement_file(char **errMsg, uint64_t apid,
    int cmdIndex, alpsc_peInfo_t *alpsc_peInfo, int controlNid,
    struct sockaddr_in controlSoc, int numBranches,
    alpsc_branchInfo_t *alpsc_branchInfo);

#endif  /* __ALPSCOMM_CN_H */
