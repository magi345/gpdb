/*-------------------------------------------------------------------------
 *
 * segment_info_sender.c
 *    Send segment information to the collector
 *
 * This file contains functions for sending segment information to
 * the collector.
 *
 *  Created on: Feb 28, 2010
 *      Author: kkrik
 *
 * Portions Copyright (c) 2010, Greenplum inc.
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 *
 *
 * IDENTIFICATION
 *	    src/backend/postmaster/segment_info_sender.c
 *
 *-------------------------------------------------------------------------
*/

#include "postgres.h"

#include <unistd.h>
#include <signal.h>

#include "postmaster/bgworker.h"
#include "postmaster/segment_info_sender.h"

#include "storage/ipc.h"
#include "storage/proc.h"
#include "storage/pmsignal.h"			/* PostmasterIsAlive */

#include "utils/metrics_utils.h"

#include "tcop/tcopprot.h"
#include "cdb/cdbvars.h"
#include "utils/vmem_tracker.h"

/* Sender-related routines */
static void SegmentInfoSenderLoop(void);

/*
 * cluster state collector hook
 * Use this hook to collect cluster wide state data periodically.
 */
cluster_state_collect_hook_type cluster_state_collect_hook = NULL;

/*
 * query info collector hook
 * Use this hook to collect real-time query information and status data.
 */
query_info_collect_hook_type query_info_collect_hook = NULL;


/**
 * This method is called after fork of the stats sender process. It sets up signal
 * handlers and does initialization that is required by a postgres backend.
 */
void SegmentInfoSenderMain(Datum main_arg)
{
	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* main loop */
	SegmentInfoSenderLoop();

	/* One iteration done, go away */
	proc_exit(0);
}

bool
SegmentInfoSenderStartRule(Datum main_arg)
{
	if (!gp_enable_query_metrics)
		return false;

	/* FIXME: even for the utility mode? */
	return true;
}

/**
 * Main loop of the sender process. It wakes up every
 * SEGMENT_INFO_LOOP_SLEEP_MS ms to send segment
 * information to the collector
 */
static void
SegmentInfoSenderLoop(void)
{
	int rc;
	int counter;

	for (counter = 0;; counter += SEGMENT_INFO_LOOP_SLEEP_MS)
	{
		if (cluster_state_collect_hook)
			cluster_state_collect_hook();

		/* Sleep a while. */
		rc = WaitLatch(&MyProc->procLatch,
				WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
				SEGMENT_INFO_LOOP_SLEEP_MS);
		ResetLatch(&MyProc->procLatch);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);
	} /* end server loop */

	return;
}
