/*****************************************************************************\
 *  sattach.c - Attach to a running job step.
 *
 *  $Id: sattach.c 8447 2006-06-26 22:29:29Z morrone $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/hostlist.h"
#include "src/common/slurm_cred.h"
#include "src/common/bitstring.h"
#include "src/common/net.h"
#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/slurm_auth.h"
#include "src/common/forward.h"
#include "src/api/step_io.h"

#include "src/sattach/opt.h"

static void print_layout_info(slurm_step_layout_t *layout);
static slurm_cred_t _generate_fake_cred(uint32_t jobid, uint32_t stepid,
					uid_t uid, char *nodelist);
static int _attach_to_tasks(uint32_t jobid,
			    uint32_t stepid,
			    slurm_step_layout_t *layout,
			    slurm_cred_t fake_cred,
			    uint16_t num_resp_port,
			    uint16_t *resp_port);

/**********************************************************************
 * Message handler declarations
 **********************************************************************/
struct message_thread_state {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int tasks_requested;
	bitstr_t *tasks_started; /* or attempted to start, but failed */
	bitstr_t *tasks_exited;  /* or never started correctly */
	bool abort;
	bool abort_action_taken;

	/* message thread variables */
	eio_handle_t *msg_handle;
	pthread_t msg_thread;
	/* set to -1 if slaunch message handler should not attempt to handle */
	uint16_t num_resp_port;
	uint16_t *resp_port; /* array of message response ports */

	/* user registered callbacks */
	slurm_job_step_launch_callbacks_t callback;
};

static int _msg_thr_create(struct message_thread_state *sls, int num_nodes);
static void _handle_msg(struct message_thread_state *sls, slurm_msg_t *msg);
static bool _message_socket_readable(eio_obj_t *obj);
static int _message_socket_accept(eio_obj_t *obj, List objs);

static struct io_operations message_socket_ops = {
	readable:	&_message_socket_readable,
	handle_read:	&_message_socket_accept
};

/**********************************************************************
 * main
 **********************************************************************/
int main(int argc, char *argv[])
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_step_layout_t *layout;
	slurm_cred_t fake_cred;
	struct message_thread_state mts;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	if (initialize_and_process_args(argc, argv) < 0) {
		fatal("salloc parameter parsing");
	}
	/* reinit log with new verbosity (if changed by command line) */
	if (opt.verbose || opt.quiet) {
		logopt.stderr_level += opt.verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	layout = slurm_job_step_layout_get(opt.jobid, opt.stepid);
	if (layout == NULL) {
		error("Could not get job step info: %m");
		return 1;
	}

	fake_cred = _generate_fake_cred(opt.jobid, opt.stepid,
					opt.uid, layout->node_list);
	
	memset(&mts, 0, sizeof(struct message_thread_state));
	_msg_thr_create(&mts, layout->node_cnt);

	print_layout_info(layout);

	_attach_to_tasks(opt.jobid, opt.stepid, layout, fake_cred,
			 mts.num_resp_port, mts.resp_port);

	slurm_job_step_layout_free(layout);

	return 0;
}

static void print_layout_info(slurm_step_layout_t *layout)
{
	hostlist_t nl;
	int i, j;

	info("node count = %d", layout->node_cnt);
	info("total task count = %d", layout->task_cnt);
	info("node names = \"%s\"", layout->node_list);
	nl = hostlist_create(layout->node_list);
	for (i = 0; i < layout->node_cnt; i++) {
		char *name = hostlist_nth(nl, i);
		info("%s: node %d, tasks %d", name, i, layout->tasks[i]);
		for (j = 0; j < layout->tasks[i]; j++) {
			info("\ttask %d", layout->tids[i][j]);
		}
		free(name);
	}
}

/* return a faked job credential */
static slurm_cred_t _generate_fake_cred(uint32_t jobid, uint32_t stepid,
					uid_t uid, char *nodelist)
{
	slurm_cred_arg_t arg;
	slurm_cred_t cred;

	arg.jobid    = jobid;
	arg.stepid   = stepid;
	arg.uid      = uid;
	arg.hostlist = nodelist;
 	cred = slurm_cred_faker(&arg);

	return cred;
}


#if 0
/*
 * Take a string representing a node list, remove the first node in the list,
 * and return an xmalloc()ed string of the remaining nodes.
 *
 * Free the returned string with xfree().
 *
 * Returns NULL on error.
 */
static char *_node_list_remove_first(const char *nodes)
{
	char *new_nodes;
	hostlist_t nodes_list;
	char *node;
	char buf[BUFSIZ];

	nodes_list = hostlist_create(nodes);
	node = hostlist_shift(nodes_list);
	free(node);
	if (hostlist_ranged_string(nodes_list, BUFSIZ, buf) == -1) {
		error("_node_list_remove_first node list truncation occurred");
		new_nodes = NULL;
	} else {
		new_nodes = xstrdup(buf);
	}

	hostlist_destroy(nodes_list);
	return new_nodes;
}
#endif

/*
 * Take a NodeNode name list in hostlist_t string format, and expand
 * it into one giant string of NodeNames, in which each NodeName is
 * found at regular offsets of MAX_SLURM_NAME bytes into the string.
 *
 * Also, it trims off the first NodeName, which is not used because we
 * send to that node directly.
 *
 * Free returned string with xfree();
 */
static char *_create_ugly_nodename_string(const char *node_list, uint32_t count)
{
	char *ugly_str;
	hostlist_t nl;
	hostlist_iterator_t itr;
	char *node;
	int i;

	ugly_str = xmalloc(MAX_SLURM_NAME *count);
	nl = hostlist_create(node_list);
	itr = hostlist_iterator_create(nl);
	
	/* skip the first node */
	free(hostlist_next(itr));

	/* now add all remaining node names up to a maximum of "count" */
	for (i = 0; (i < count) && ((node = hostlist_next(itr)) != NULL); i++) {
		strcpy(ugly_str + (i*MAX_SLURM_NAME), node);
		free(node);
	}

	hostlist_iterator_destroy(itr);
	hostlist_destroy(nl);
	return ugly_str;
}

/*
 * Create a simple array of sequential uint32_t values from "first" to "last".
 * For example, if "first" is 3 and "last" is 8, the array would contain:
 *     3, 4, 5, 6, 7, 8
 *
 * Free the returned array with xfree().
 *
 * Returns NULL on error.
 */
static uint32_t *_create_range_array(uint32_t first, uint32_t last)
{
	uint32_t i, current, len, *array;

	if (first > last) {
		error("_create_range_array, \"first\""
		      " cannot be greater than \"last\"");
		return NULL;
	}

	len = last - first + 1;
	array = xmalloc(sizeof(uint32_t) * len);
	for (i = 0, current = first; i < len; i++, current++) {
		array[i] = current;
	}

	return array;
}


static int _attach_to_tasks(uint32_t jobid,
			    uint32_t stepid,
			    slurm_step_layout_t *layout,
			    slurm_cred_t fake_cred,
			    uint16_t num_resp_port,
			    uint16_t *resp_port)
{
	slurm_msg_t msg, dummy_resp_msg;
	List ret_list = NULL;
	ListIterator ret_itr;
	ListIterator ret_data_itr;
	ret_types_t *ret;
	ret_data_info_t *ret_data;
	int timeout;
	reattach_tasks_request_msg_t reattach_msg;

	debug("Entering _attach_to_tasks");
	/* Lets make sure that the slurm_msg_t are zeroed out at the start */
	memset(&msg, 0, sizeof(slurm_msg_t));
	memset(&dummy_resp_msg, 0, sizeof(slurm_msg_t));

	timeout = slurm_get_msg_timeout();

	reattach_msg.job_id = jobid;
	reattach_msg.job_step_id = stepid;
	reattach_msg.num_resp_port = num_resp_port;
	reattach_msg.resp_port = resp_port; /* array or response ports */
	reattach_msg.num_io_port = 0; /* FIXME */
	reattach_msg.io_port = NULL; /* FIXME */
	reattach_msg.cred = fake_cred;

	msg.msg_type = REQUEST_REATTACH_TASKS;
	msg.data = &reattach_msg;
	msg.srun_node_id = 0;
	msg.orig_addr.sin_addr.s_addr = 0;
	forward_init(&msg.forward, NULL);
	msg.forward.cnt = layout->node_cnt - 1;
	msg.forward.node_id = _create_range_array(1, layout->node_cnt-1);
	info("msg.forward.cnt = %d", msg.forward.cnt);
	msg.forward.name = _create_ugly_nodename_string(layout->node_list,
							layout->node_cnt-1);
	info("msg.forward.name = %s", msg.forward.name);
	msg.forward.addr = layout->node_addr + 1;
	msg.forward.timeout = timeout * 1000; /* sec to msec */
	msg.forward_struct = NULL;
	msg.forward_struct_init = 0;
	msg.ret_list = NULL;
	memcpy(&msg.address, layout->node_addr + 0, sizeof(slurm_addr));

	ret_list = slurm_send_recv_node_msg(&msg, &dummy_resp_msg, timeout);
	if (ret_list == NULL) {
		error("slurm_send_recv_node_msg failed: %m");
		xfree(msg.forward.node_id);
		xfree(msg.forward.name);
		return SLURM_ERROR;
	}

	ret_itr = list_iterator_create(ret_list);
	while ((ret = list_next(ret_itr)) != NULL) {
		debug("launch returned msg_rc=%d err=%d type=%d",
		      ret->msg_rc, ret->err, ret->type);
		if (ret->msg_rc != SLURM_SUCCESS) {
			ret_data_itr =
				list_iterator_create(ret->ret_data_list);
			while ((ret_data = list_next(ret_data_itr)) != NULL) {
				errno = ret->err;
				error("Attach failed on node %s(%d): %m",
				      ret_data->node_name, ret_data->nodeid);
			}
			list_iterator_destroy(ret_data_itr);
		} else {
			ret_data_itr =
				list_iterator_create(ret->ret_data_list);
			while ((ret_data = list_next(ret_data_itr)) != NULL) {
				errno = ret->err;
				info("Attach success on node %s(%d)",
				     ret_data->node_name, ret_data->nodeid);
			}
			list_iterator_destroy(ret_data_itr);
		}
	}
	xfree(msg.forward.node_id);
	xfree(msg.forward.name);
	list_iterator_destroy(ret_itr);
	list_destroy(ret_list);
	return SLURM_SUCCESS;
}




/**********************************************************************
 * Message handler functions
 **********************************************************************/
static void *_msg_thr_internal(void *arg)
{
	struct message_thread_state *mts = (struct message_thread_state *)arg;

	eio_handle_mainloop(mts->msg_handle);

	return NULL;
}

static inline int
_estimate_nports(int nclients, int cli_per_port)
{
	div_t d;
	d = div(nclients, cli_per_port);
	return d.rem > 0 ? d.quot + 1 : d.quot;
}

static int _msg_thr_create(struct message_thread_state *mts, int num_nodes)
{
	int sock = -1;
	int port = -1;
	eio_obj_t *obj;
	int i;

	debug("Entering _msg_thr_create()");

	mts->msg_handle = eio_handle_create();
	mts->num_resp_port = _estimate_nports(num_nodes, 48);
	mts->resp_port = xmalloc(sizeof(uint16_t) * mts->num_resp_port);
	for (i = 0; i < mts->num_resp_port; i++) {
		if (net_stream_listen(&sock, &port) < 0) {
			error("unable to intialize step launch listening socket: %m");
			return SLURM_ERROR;
		}
		mts->resp_port[i] = ntohs(port);
		obj = eio_obj_create(sock, &message_socket_ops, (void *)mts);
		eio_new_initial_obj(mts->msg_handle, obj);
	}

	if (pthread_create(&mts->msg_thread, NULL,
			   _msg_thr_internal, (void *)mts) != 0) {
		error("pthread_create of message thread: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static bool _message_socket_readable(eio_obj_t *obj)
{
	debug3("Called _message_socket_readable");
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			debug2("  false, shutdown");
			close(obj->fd);
			obj->fd = -1;
			/*_wait_for_connections();*/
		} else {
			debug2("  false");
		}
		return false;
	}
	return true;
}

static int _message_socket_accept(eio_obj_t *obj, List objs)
{
	struct message_thread_state *mts = (struct message_thread_state *)obj->arg;

	int fd;
	unsigned char *uc;
	short        port;
	struct sockaddr_un addr;
	slurm_msg_t *msg = NULL;
	int len = sizeof(addr);
	int          timeout = 0;	/* slurm default value */
	List ret_list = NULL;

	debug3("Called _msg_socket_accept");

	while ((fd = accept(obj->fd, (struct sockaddr *)&addr,
			    (socklen_t *)&len)) < 0) {
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN
		    || errno == ECONNABORTED
		    || errno == EWOULDBLOCK) {
			return SLURM_SUCCESS;
		}
		error("Error on msg accept socket: %m");
		obj->shutdown = true;
		return SLURM_SUCCESS;
	}

	fd_set_close_on_exec(fd);
	fd_set_blocking(fd);

	/* Should not call slurm_get_addr() because the IP may not be
	   in /etc/hosts. */
	uc = (unsigned char *)&((struct sockaddr_in *)&addr)->sin_addr.s_addr;
	port = ((struct sockaddr_in *)&addr)->sin_port;
	debug2("got message connection from %u.%u.%u.%u:%d",
	       uc[0], uc[1], uc[2], uc[3], ntohs(port));
	fflush(stdout);

	msg = xmalloc(sizeof(slurm_msg_t));
	forward_init(&msg->forward, NULL);
	msg->ret_list = NULL;
	msg->conn_fd = fd;
	msg->forward_struct_init = 0;

	/* multiple jobs (easily induced via no_alloc) and highly
	 * parallel jobs using PMI sometimes result in slow message 
	 * responses and timeouts. Raise the default timeout for srun. */
	timeout = slurm_get_msg_timeout() * 8;
again:
	ret_list = slurm_receive_msg(fd, msg, timeout);
	if(!ret_list || errno != SLURM_SUCCESS) {
		if (errno == EINTR) {
			list_destroy(ret_list);
			goto again;
		}
		error("slurm_receive_msg[%u.%u.%u.%u]: %m",
		      uc[0],uc[1],uc[2],uc[3]);
		goto cleanup;
	}
	if(list_count(ret_list)>0) {
		error("_message_socket_accept connection: "
		      "got %d from receive, expecting 0",
		      list_count(ret_list));
	}
	msg->ret_list = ret_list;

	_handle_msg(mts, msg); /* handle_msg frees msg */
cleanup:
	if ((msg->conn_fd >= 0) && slurm_close_accepted_conn(msg->conn_fd) < 0)
		error ("close(%d): %m", msg->conn_fd);
	slurm_free_msg(msg);

	return SLURM_SUCCESS;
}

static void
_launch_handler(struct message_thread_state *mts, slurm_msg_t *resp)
{
	launch_tasks_response_msg_t *msg = resp->data;
	int i;

	pthread_mutex_lock(&mts->lock);

	for (i = 0; i < msg->count_of_pids; i++) {
		bit_set(mts->tasks_started, msg->task_ids[i]);
	}

	if (mts->callback.task_start != NULL)
		(mts->callback.task_start)(msg);

	pthread_cond_signal(&mts->cond);
	pthread_mutex_unlock(&mts->lock);

}

static void 
_exit_handler(struct message_thread_state *mts, slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg = (task_exit_msg_t *) exit_msg->data;
	int i;

	pthread_mutex_lock(&mts->lock);

	for (i = 0; i < msg->num_tasks; i++) {
		debug("task %d done", msg->task_id_list[i]);
		bit_set(mts->tasks_exited, msg->task_id_list[i]);
	}

	if (mts->callback.task_finish != NULL)
		(mts->callback.task_finish)(msg);

	pthread_cond_signal(&mts->cond);
	pthread_mutex_unlock(&mts->lock);
}

static void
_handle_msg(struct message_thread_state *mts, slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred);
	static uid_t slurm_uid;
	static bool slurm_uid_set = false;
	uid_t uid = getuid();
	
	if (!slurm_uid_set) {
		slurm_uid = slurm_get_slurm_user_id();
		slurm_uid_set = true;
	}

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u", 
		       (unsigned int) req_uid);
		return;
	}

	switch (msg->msg_type) {
	case RESPONSE_LAUNCH_TASKS:
		debug2("received task launch");
		_launch_handler(mts, msg);
		slurm_free_launch_tasks_response_msg(msg->data);
		break;
	case MESSAGE_TASK_EXIT:
		debug2("received task exit");
		_exit_handler(mts, msg);
		slurm_free_task_exit_msg(msg->data);
		break;
	case SRUN_JOB_COMPLETE:
		debug2("received job step complete message");
		/* FIXME - does nothing yet */
		slurm_free_srun_job_complete_msg(msg->data);
		break;
	default:
		error("received spurious message type: %d",
		      msg->msg_type);
		break;
	}
	return;
}
