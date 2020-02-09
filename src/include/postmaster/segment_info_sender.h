/*-------------------------------------------------------------------------
 *
 * segment_info_sender.h
 *	  Definitions for segment info sender process.
 *
 * This file contains the basic interface that is needed by postmaster
 * to start the segment info sender process.
 *
 *
 * Portions Copyright (c) 2010, Greenplum inc
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 *
 *
 * IDENTIFICATION
 *	    src/include/postmaster/segment_info_sender.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef SEGMENT_INFO_SENDER_H
#define SEGMENT_INFO_SENDER_H

/* Interface */
void SegmentInfoSenderMain(Datum main_arg);
bool SegmentInfoSenderStartRule(Datum main_arg);

typedef void (*cluster_state_collect_hook_type)(void);
extern PGDLLIMPORT cluster_state_collect_hook_type cluster_state_collect_hook;

#define SEGMENT_INFO_LOOP_SLEEP_MS (100)

#endif /* SEGMENT_INFO_SENDER_H */
