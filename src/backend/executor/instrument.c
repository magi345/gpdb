/*-------------------------------------------------------------------------
 *
 * instrument.c
 *	 functions for instrumentation of plan execution
 *
 *
 * Portions Copyright (c) 2006-2009, Greenplum inc
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 * Copyright (c) 2001-2009, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/executor/instrument.c,v 1.20 2008/01/01 19:45:49 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>
#include <cdb/cdbvars.h>

#include "storage/spin.h"
#include "executor/instrument.h"

InstrumentationHeader *InstrumentGlobal = NULL;

/* Allocate a header and an array of Instrumentation slots */
Size
InstrShmemSize(void)
{
	return sizeof(InstrumentationHeader) + MaxInstrumentationOnShmem * sizeof(InstrumentationSlot);
}

/* Initialize Shmem space to construct a free list of Instrumentation */
void
InstrShmemInit(void)
{
	Size size = InstrShmemSize();
	InstrumentationSlot *slot;
	InstrumentationHeader *header;
	int i;

	/* Allocate space from Shmem */
	header = (InstrumentationHeader *)ShmemAlloc(size);
	if (!header) {
		ereport(FATAL, (errcode(ERRCODE_OUT_OF_MEMORY), errmsg("out of shared memory")));
		return;
	}

	/* Initialize header and all slots to zeroes, then modify as needed */
	MemSet(header, 0x00, size);

	/* pointer to the first Instrumentation slot */
	slot = (InstrumentationSlot*)(header + 1);

	/* header points to the first slot */
	header->head = slot;
	header->in_use = 0;
	header->free = MaxInstrumentationOnShmem;
	SpinLockInit(&header->stat_lck);

	/* Each slot points to next one to construct the free list */
	for (i = 0; i < MaxInstrumentationOnShmem - 1; i++)
		GetInstrumentNext(&slot[i]) = &slot[i + 1];
	GetInstrumentNext(&slot[i]) = NULL;

	/* Finished init the free list */
	InstrumentGlobal = header;
}

/* Allocate new instrumentation structure(s) */
Instrumentation *
InstrAlloc(int n)
{
	/* GPDB not support trigger */
	Assert(1 == n);

	Instrumentation *instr = NULL;
	InstrumentationSlot *slot = NULL;

	if (gp_enable_query_metrics && NULL != InstrumentGlobal)
	{
		/* When query metrics on and instrumentation slots on Shmem is initialized */
		SpinLockAcquire(&InstrumentGlobal->stat_lck);

		/* Pick the first free slot */
		slot = InstrumentGlobal->head;
		if (NULL != slot)
		{
			/* Header points to the next free slot */
			InstrumentGlobal->head = GetInstrumentNext(slot);
			InstrumentGlobal->free--;
			InstrumentGlobal->in_use++;
		}
		SpinLockRelease(&InstrumentGlobal->stat_lck);

		if (NULL != slot) {
			/* initialize the picked slot */
			GetInstrumentNext(slot) = NULL;
			instr = &(slot->data);
			instr->in_shmem = true;
			slot->segid = Gp_segment;
			slot->pid = MyProcPid;
		}
	}

	if (NULL == instr)
	{
		/* Alloc Instrumentation in local memory */
		/* When gp_enable_query_metrics is off, the Instrumentation reside on local memory by default */
		/* When gp_enable_query_metrics is on but failed to pick a slot, also fallback to local memory */
		instr = palloc0(n * sizeof(Instrumentation));
	}

	/* we don't need to do any initialization except zero 'em */
	instr->numPartScanned = 0;

	return instr;
}

void
InstrFree(Instrumentation *instr)
{
	InstrumentationSlot *slot;

	if (NULL == instr)
		return;
	if (NULL == InstrumentGlobal)
		return;

	/* Recycle Instrumentation slot back to the free list */
	if (instr->in_shmem) {
		slot = (InstrumentationSlot*) instr;
		MemSet(slot, 0x00, sizeof(InstrumentationSlot));

		SpinLockAcquire(&InstrumentGlobal->stat_lck);

		GetInstrumentNext(slot) = InstrumentGlobal->head;
		InstrumentGlobal->head = slot;
		InstrumentGlobal->free++;
		InstrumentGlobal->in_use--;

		SpinLockRelease(&InstrumentGlobal->stat_lck);
	}
}

/* Entry to a plan node */
void
InstrStartNode(Instrumentation *instr)
{
	if (INSTR_TIME_IS_ZERO(instr->starttime))
		INSTR_TIME_SET_CURRENT(instr->starttime);
	else
		elog(DEBUG2, "InstrStartNode called twice in a row");
}

/* Exit from a plan node */
void
InstrStopNode(Instrumentation *instr, double nTuples)
{
	instr_time	endtime;

	/* count the returned tuples */
	instr->tuplecount += nTuples;

	if (INSTR_TIME_IS_ZERO(instr->starttime))
	{
		elog(DEBUG2, "InstrStopNode called without start");
		return;
	}

	INSTR_TIME_SET_CURRENT(endtime);
	INSTR_TIME_ACCUM_DIFF(instr->counter, endtime, instr->starttime);

	/* Is this the first tuple of this cycle? */
	if (!instr->running)
	{
		instr->running = true;
		instr->firsttuple = INSTR_TIME_GET_DOUBLE(instr->counter);
		/* CDB: save this start time as the first start */
		instr->firststart = instr->starttime;
	}

	INSTR_TIME_SET_ZERO(instr->starttime);
}

/* Finish a run cycle for a plan node */
void
InstrEndLoop(Instrumentation *instr)
{
	double		totaltime;

	/* Skip if nothing has happened, or already shut down */
	if (!instr->running)
		return;

	if (!INSTR_TIME_IS_ZERO(instr->starttime))
		elog(DEBUG2, "InstrEndLoop called on running node");

	/* Accumulate per-cycle statistics into totals */
	totaltime = INSTR_TIME_GET_DOUBLE(instr->counter);

	/* CDB: Report startup time from only the first cycle. */
	if (instr->nloops == 0)
		instr->startup = instr->firsttuple;

	instr->total += totaltime;
	instr->ntuples += instr->tuplecount;
	instr->nloops += 1;

	/* Reset for next cycle (if any) */
	instr->running = false;
	INSTR_TIME_SET_ZERO(instr->starttime);
	INSTR_TIME_SET_ZERO(instr->counter);
	instr->firsttuple = 0;
	instr->tuplecount = 0;
}
