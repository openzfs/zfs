#ifndef _SPL_SIGNAL_H
#define _SPL_SIGNAL_H

#define	FORREAL		0	/* Usual side-effects */
#define	JUSTLOOKING	1	/* Don't stop the process */

/* The "why" argument indicates the allowable side-effects of the call:
 *
 * FORREAL:  Extract the next pending signal from p_sig into p_cursig;
 * stop the process if a stop has been requested or if a traced signal
 * is pending.
 *
 * JUSTLOOKING:  Don't stop the process, just indicate whether or not
 * a signal might be pending (FORREAL is needed to tell for sure).
 */
static __inline__ int
issig(int why)
{
	ASSERT(why == FORREAL || why == JUSTLOOKING);

	return signal_pending(current);
}

#endif /* SPL_SIGNAL_H */
