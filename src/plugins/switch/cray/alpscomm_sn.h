/*
 * (c) 2013 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

#ifndef __ALPSCOMM_SN_H
#define __ALPSCOMM_SN_H

#include <stdint.h>

#ident "$Id: alpscomm_sn.h 8196 2013-07-16 14:27:01Z ben $"

/*
 * libalpscomm_sn - an external library interface for service node
 * services which are common to both ALPS and native workload managers.
 */

/* ***************************************************************
 *                    Cookie APIs
 * ***************************************************************/

#define ALPSC_NO_OWNER       NULL
#define ALPSC_NO_DOMAIN      0
#define ALPSC_INFINITE_LEASE 0

/*
 * alpsc_lease_cookies - request cookies to be used on a set of nodes
 * for a specified period of time.  Either num_cookies are allocated, or
 * (on error) zero cookies are allocated.
 *
 * Arguments
 *   errMsg - returns a fatal error message if return != 0; the caller is
 *            responsible to free the memory allocated for a message.
 *   owner - string identifier for requestor, i.e., you.  Set to ALPSC_NO_OWNER
 *           if not applicable (NULL).  String should be NULL-terminated.
 *           Used in status displays only.
 *   domain - arbitrary identifier for entity that will use cookies.  Set to
 *            ALPSC_NO_DOMAIN if not applicable (0).  Used in status
 *            displays only.
 *   lease_time - duration, in seconds, the requested cookies will remain
 *                allocated to the requestor.  Allocated cookies should be
 *                refreshed (see alpsc_set_cookie_lease()) or released (see
 *                alpsc_release_cookies()) prior to lease expiration.  Set
 *                to ALPSC_INFINITE_LEASE (0) for an infinite lease.
 *   nodes - array of node identifiers that will use the the requested cookies.
 *           Each node in the system should have a system-unique identifier.
 *           The cookie allocation algorithm does not care what the identifiers
 *           are, only that they be system unique.
 *           *** It is not necessary, but HIGHLY RECOMMENDED that this array
 *           be in sorted ascending order.
 *   num_nodes - number of entries in nodes, above
 *   num_cookies - number of cookies being requested
 *   cookies - function will store cookie values here as NULL-terminated
 *             strings (success only).  The caller is responsible for free()ing
 *             the array contents and the array itself.
 *   cookie_ids - function will store cookie ids here (success only).  The
 *                caller is responsible for free()ing the array itself.
 *                cookies[x] has id cookie_ids[x]
 *
 *
 * Returns
 *    0 if successful, >0 failure (see below)
 *    1 upon server connection issue
 *    2 upon memory allocation failure
 *    3 upon invalid argument (e.g., NULL nodes, cookies, cookie_ids;
 *                             0 for num_cookies)
 *    4 when the server indicates there aren't enough cookies available to
 *      satisfy the request
 */
extern int alpsc_lease_cookies(char **errMsg,
			       const char *owner, int64_t domain,
			       int32_t lease_time, const int32_t *nodes,
			       int32_t num_nodes, int32_t num_cookies,
			       char ***cookies, int32_t **cookie_ids);

/*
 * alpsc_release_cookies - release the cookies associated with the specified
 * cookie ids.  Either all cookies are released or none are released.
 *
 * Arguments
 *   errMsg - returns a fatal error message; the caller is responsible
 *            to free the memory allocated for a message.
 *   cookie_ids - array of cookie ids to release
 *                *** It is not necessary, but HIGHLY RECOMMENDED that this
 *                array be in sorted ascending order.
 *   num_cookie_ids - number of cookie ids in cookie_ids, above
 *
 * Returns
 *    0 if successful, >0 failure (see below)
 *    1 upon server connection issue
 *    2 upon memory allocation failure
 *    3 upon invalid argument (e.g., NULL cookie_ids, num_cookie_ids <= 0)
 *    4 upon one or more cookies specified in cookie_ids not being able to
 *      be released
 */
extern int alpsc_release_cookies(char **errMsg,
				 const int32_t *cookie_ids, int32_t num_cookie_ids);

/*
 * alpsc_set_cookie_lease - sets the lease time for the cookies associated with the specified
 * cookie ids to a new value.  Either all cookies specified have their lease
 *  set to the new value or none of them are changed.
 *
 * Arguments
 *   errMsg - returns a fatal error message; the caller is responsible
 *            to free the memory allocated for a message.
 *   cookie_ids - array of cookie ids to release
 *   num_cookie_ids - number of cookie ids in cookie_ids, above
 *   lease_time - duration, in seconds, the specified cookies will have their
 *                lease time set to; overrides the value specified via
 *                alpsc_lease_cookies() or a previous alpsc_set_cookie_lease().
 *                Set to 0 for an infinite lease.
 *
 * Returns
 *    0 if successful, >0 failure (see below)
 *    1 upon server connection issue
 *    2 upon memory allocation failure
 *    3 upon invalid argument (e.g., NULL cookie_ids, num_cookie_ids <= 0,
 *                             lease_time < 0)
 *    4 upon one or more cookies specified in cookie_ids not being able to
 *      have its lease time modified
 */
extern int alpsc_set_cookie_lease(char **errMsg,
				  const int32_t *cookie_ids,
				  int32_t num_cookie_ids,
				  int32_t lease_time);


/*
 * alpsc_view_cookies - retrieves all cookies and associated data that are
 * currently allocated or deallocated but still unavailable for new allocations.
 * This data can be further filtered by supplying an optional owner and/or
 * domain argument.
 *
 * Arguments
 *   errMsg - returns a fatal error message; the caller is responsible
 *            to free the memory allocated for a message.
 *   filter_owner - filter for owner string identifier, i.e., you.  Set to
 *                  ALPSC_NO_OWNER if not applicable (NULL).
 *                  String should be NULL-terminated.
 *   filter_domain - arbitrary identifier for entity that will use cookies.  Set
 *                   to ALPSC_NO_DOMAIN if not applicable (0).  Used in
 *                   status displays only.
 *   results - array of structures, with each structure containing cookie data
 *   num_results - number of entries in results, above
 *
 * Caller is responsible for freeing each array element's owner and cookie
 * fields, as well as the array itself, e.g.,
 *
 * alpsc_view_cookies(&msg_buf, NULL, 0, &rows, &num_rows);
 * // Process data
 * // ...
 * for (i = 0; i < num_rows && rows && rows[i].cookie; i++) {
 *	free(rows[i].owner);
 *	free(rows[i].cookie);
 * }
 * free(rows);
 *
 * Returns
 *    0 if successful, >0 failure (see below)
 *    1 upon server connection issue
 *    2 upon memory allocation failure
 */
typedef struct alpsc_cookie_row_s {
    char *owner;
    int64_t domain;
    /*
     * Unix epoch UTC.
     * A negative values indicates a cookie is "cooling off" before being
     * available for future allocations.  The cookie will be available to future
     * allocations, at the earliest, at -1 * lease_expire.
     */
    int64_t lease_expire;
    char *cookie;
    int32_t cookie_id;
} alpsc_cookie_row_t;

extern int alpsc_view_cookies(char **errMsg,
			      const char *filter_owner, int64_t filter_domain,
			      alpsc_cookie_row_t **results,
			      int32_t *num_results);

#endif /* __ALPSCOMM_SN_H */
