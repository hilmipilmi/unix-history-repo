/*	subr_xxx.c	4.12	82/07/15	*/

#include "../h/param.h"
#include "../h/systm.h"
#include "../h/conf.h"
#include "../h/inode.h"
#include "../h/dir.h"
#include "../h/user.h"
#include "../h/buf.h"
#include "../h/proc.h"
#include "../h/fs.h"
#include "../h/vm.h"
#include "../h/pte.h"
#include "../h/cmap.h"

/*
 * Pass back  c  to the user at his location u_base;
 * update u_base, u_count, and u_offset.  Return -1
 * on the last character of the user's read.
 * u_base is in the user address space unless u_segflg is set.
 */
passc(c)
register c;
{
	register id;

	if ((id = u.u_segflg) == 1)
		*u.u_base = c;
	else
		if (id?suibyte(u.u_base, c):subyte(u.u_base, c) < 0) {
			u.u_error = EFAULT;
			return (-1);
		}
	u.u_count--;
	u.u_offset++;
	u.u_base++;
	return (u.u_count == 0? -1: 0);
}

#include "ct.h"
#if NCT > 0
/*
 * Pick up and return the next character from the user's
 * write call at location u_base;
 * update u_base, u_count, and u_offset.  Return -1
 * when u_count is exhausted.  u_base is in the user's
 * address space unless u_segflg is set.
 */
cpass()
{
	register c, id;

	if (u.u_count == 0)
		return (-1);
	if ((id = u.u_segflg) == 1)
		c = *u.u_base;
	else
		if ((c = id==0?fubyte(u.u_base):fuibyte(u.u_base)) < 0) {
			u.u_error = EFAULT;
			return (-1);
		}
	u.u_count--;
	u.u_offset++;
	u.u_base++;
	return (c&0377);
}
#endif

/*
 * Routine which sets a user error; placed in
 * illegal entries in the bdevsw and cdevsw tables.
 */
nodev()
{

	u.u_error = ENODEV;
}

/*
 * Null routine; placed in insignificant entries
 * in the bdevsw and cdevsw tables.
 */
nulldev()
{

}

imin(a, b)
{

	return (a < b ? a : b);
}

imax(a, b)
{

	return (a > b ? a : b);
}

unsigned
min(a, b)
	unsigned int a, b;
{

	return (a < b ? a : b);
}

unsigned
max(a, b)
	unsigned int a, b;
{

	return (a > b ? a : b);
}

struct proc *
pfind(pid)
	int pid;
{
	register struct proc *p;

	for (p = &proc[pidhash[PIDHASH(pid)]]; p != &proc[0]; p = &proc[p->p_idhash])
		if (p->p_pid == pid)
			return (p);
	return ((struct proc *)0);
}
extern	cabase, calimit;
extern	struct pte camap[];

caddr_t	cacur = (caddr_t)&cabase;
caddr_t	camax = (caddr_t)&cabase;
int	cax = 0;
/*
 * This is a kernel-mode storage allocator.
 * It is very primitive, currently, in that
 * there is no way to give space back.
 * It serves, for the time being, the needs of
 * auto-configuration code and the like which
 * need to allocate some stuff at boot time.
 */
caddr_t
calloc(size)
	int size;
{
	register caddr_t res;
	register int i;

	if (cacur+size >= (caddr_t)&calimit)
		panic("calloc");
	while (cacur+size > camax) {
		(void) vmemall(&camap[cax], CLSIZE, &proc[0], CSYS);
		vmaccess(&camap[cax], camax, CLSIZE);
		for (i = 0; i < CLSIZE; i++)
			clearseg(camap[cax++].pg_pfnum);
		camax += NBPG * CLSIZE;
	}
	res = cacur;
	cacur += size;
	return (res);
}
#ifndef vax
ffs(mask)
	register long mask;
{
	register int i;

	for(i=1; i<NSIG; i++) {
		if (mask & 1)
			return (i);
		mask >>= 1;
	}
	return (0);
}
#endif
