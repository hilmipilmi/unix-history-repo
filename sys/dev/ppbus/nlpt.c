/*
 * Copyright (c) 1990 William F. Jolitz, TeleMuse
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This software is a component of "386BSD" developed by
 *	William F. Jolitz, TeleMuse.
 * 4. Neither the name of the developer nor the name "386BSD"
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS A COMPONENT OF 386BSD DEVELOPED BY WILLIAM F. JOLITZ
 * AND IS INTENDED FOR RESEARCH AND EDUCATIONAL PURPOSES ONLY. THIS
 * SOFTWARE SHOULD NOT BE CONSIDERED TO BE A COMMERCIAL PRODUCT.
 * THE DEVELOPER URGES THAT USERS WHO REQUIRE A COMMERCIAL PRODUCT
 * NOT MAKE USE OF THIS WORK.
 *
 * FOR USERS WHO WISH TO UNDERSTAND THE 386BSD SYSTEM DEVELOPED
 * BY WILLIAM F. JOLITZ, WE RECOMMEND THE USER STUDY WRITTEN
 * REFERENCES SUCH AS THE  "PORTING UNIX TO THE 386" SERIES
 * (BEGINNING JANUARY 1991 "DR. DOBBS JOURNAL", USA AND BEGINNING
 * JUNE 1991 "UNIX MAGAZIN", GERMANY) BY WILLIAM F. JOLITZ AND
 * LYNNE GREER JOLITZ, AS WELL AS OTHER BOOKS ON UNIX AND THE
 * ON-LINE 386BSD USER MANUAL BEFORE USE. A BOOK DISCUSSING THE INTERNALS
 * OF 386BSD ENTITLED "386BSD FROM THE INSIDE OUT" WILL BE AVAILABLE LATE 1992.
 *
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPER ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE DEVELOPER BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: unknown origin, 386BSD 0.1
 *	From Id: lpt.c,v 1.55.2.1 1996/11/12 09:08:38 phk Exp
 *	$Id$
 */

/*
 * Device Driver for AT parallel printer port
 * Written by William Jolitz 12/18/90
 */

/*
 * Updated for ppbus by Nicolas Souchu
 * [Mon Jul 28 1997]
 */

#include <sys/types.h>

#ifdef KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/syslog.h>
#include <sys/malloc.h>

#include <machine/stdarg.h>
#include <machine/clock.h>
#include <machine/lpt.h>

#include <i386/isa/isa.h>
#include <i386/isa/isa_device.h>

#include <sys/kernel.h>
#endif /*KERNEL */

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/nlpt.h>

#ifndef DEBUG
#define nlprintf (void)
#else
#define nlprintf		if (nlptflag) printf
static int volatile nlptflag = 1;
#endif

#define	LPINITRDY	4	/* wait up to 4 seconds for a ready */
#define	LPTOUTINITIAL	10	/* initial timeout to wait for ready 1/10 s */
#define	LPTOUTMAX	1	/* maximal timeout 1 s */
#define	LPPRI		(PZERO+8)
#define	BUFSIZE		1024

#define	LPTUNIT(s)	((s)&0x03)
#define	LPTFLAGS(s)	((s)&0xfc)

static int	nlpt = 0;
#define MAXLPT	8			/* XXX not much better! */
static struct lpt_data *lptdata[MAXLPT];

/*
 * Make ourselves visible as a ppbus driver
 */

static struct ppb_device	*nlptprobe(struct ppb_data *ppb);
static int			nlptattach(struct ppb_device *dev);
static void			nlptintr(int unit);

#ifdef KERNEL

static struct ppb_driver nlptdriver = {
    nlptprobe, nlptattach, "nlpt"
};
DATA_SET(ppbdriver_set, nlptdriver);

#endif /* KERNEL */

/* bits for state */
#define	OPEN		(1<<0)	/* device is open */
#define	ASLP		(1<<1)	/* awaiting draining of printer */
#define	EERROR		(1<<2)	/* error was received from printer */
#define	OBUSY		(1<<3)	/* printer is busy doing output */
#define LPTOUT		(1<<4)	/* timeout while not selected */
#define TOUT		(1<<5)	/* timeout while not selected */
#define INIT		(1<<6)	/* waiting to initialize for open */
#define INTERRUPTED	(1<<7)	/* write call was interrupted */


/* status masks to interrogate printer status */
#define RDY_MASK	(LPS_SEL|LPS_OUT|LPS_NBSY|LPS_NERR)	/* ready ? */
#define LP_READY	(LPS_SEL|LPS_NBSY|LPS_NERR)

/* Printer Ready condition  - from lpa.c */
/* Only used in polling code */
#define	LPS_INVERT	(LPS_NBSY | LPS_NACK |           LPS_SEL | LPS_NERR)
#define	LPS_MASK	(LPS_NBSY | LPS_NACK | LPS_OUT | LPS_SEL | LPS_NERR)
#define	NOT_READY(lpt)	((ppb_rstr(&(lpt)->lpt_dev)^LPS_INVERT)&LPS_MASK)

#define	MAX_SLEEP	(hz*5)	/* Timeout while waiting for device ready */
#define	MAX_SPIN	20	/* Max delay for device ready in usecs */

static	d_open_t	nlptopen;
static	d_close_t	nlptclose;
static	d_write_t	nlptwrite;
static	d_ioctl_t	nlptioctl;

#define CDEV_MAJOR 16
static struct cdevsw nlpt_cdevsw = 
	{ nlptopen,	nlptclose,	noread,		nlptwrite,	/*16*/
	  nlptioctl,	nullstop,	nullreset,	nodevtotty,/* lpt */
	  seltrue,	nommap,		nostrat,	"nlpt",	NULL,	-1 };

/*
 * Internal routine to nlptprobe to do port tests of one byte value
 */
static int
nlpt_port_test(struct lpt_data *lpt, u_char data, u_char mask)
{
	int	temp, timeout;

	data = data & mask;
	ppb_wdtr(&lpt->lpt_dev, data);
	timeout = 10000;
	do {
		DELAY(10);
		temp = ppb_rdtr(&lpt->lpt_dev) & mask;
	}
	while (temp != data && --timeout);
	nlprintf("out=%x\tin=%x\ttout=%d\n", data, temp, timeout);
	return (temp == data);
}

/*
 * New lpt port probe Geoff Rehmet - Rhodes University - 14/2/94
 * Based partially on Rod Grimes' printer probe
 *
 * Logic:
 *	1) If no port address was given, use the bios detected ports
 *	   and autodetect what ports the printers are on.
 *	2) Otherwise, probe the data port at the address given,
 *	   using the method in Rod Grimes' port probe.
 *	   (Much code ripped off directly from Rod's probe.)
 *
 * Comments from Rod's probe:
 * Logic:
 *	1) You should be able to write to and read back the same value
 *	   to the data port.  Do an alternating zeros, alternating ones,
 *	   walking zero, and walking one test to check for stuck bits.
 *
 *	2) You should be able to write to and read back the same value
 *	   to the control port lower 5 bits, the upper 3 bits are reserved
 *	   per the IBM PC technical reference manauls and different boards
 *	   do different things with them.  Do an alternating zeros, alternating
 *	   ones, walking zero, and walking one test to check for stuck bits.
 *
 *	   Some printers drag the strobe line down when the are powered off
 * 	   so this bit has been masked out of the control port test.
 *
 *	   XXX Some printers may not like a fast pulse on init or strobe, I
 *	   don't know at this point, if that becomes a problem these bits
 *	   should be turned off in the mask byte for the control port test.
 *
 *	   We are finally left with a mask of 0x14, due to some printers
 *	   being adamant about holding other bits high ........
 *
 *	   Before probing the control port, we write a 0 to the data port -
 *	   If not, some printers chuck out garbage when the strobe line
 *	   gets toggled.
 *
 *	3) Set the data and control ports to a value of 0
 *
 *	This probe routine has been tested on Epson Lx-800, HP LJ3P,
 *	Epson FX-1170 and C.Itoh 8510RM
 *	printers.
 *	Quick exit on fail added.
 */
static int
nlpt_detect(struct lpt_data *lpt)
{
	int		status;
	u_char		data;
	u_char		mask;
	int		i;

	status = IO_LPTSIZE;

	if (ppb_request_bus(&lpt->lpt_dev, PPB_DONTWAIT)) {
		printf("nlpt: cannot alloc ppbus!\n");
		status = 0 ; goto end_probe ;
	}

	mask = 0xff;
	data = 0x55;				/* Alternating zeros */
	if (!nlpt_port_test(lpt, data, mask))
		{ status = 0 ; goto end_probe ; }

	data = 0xaa;				/* Alternating ones */
	if (!nlpt_port_test(lpt, data, mask))
		{ status = 0 ; goto end_probe ; }

	for (i = 0; i < 8; i++)	{		/* Walking zero */
		data = ~(1 << i);
		if (!nlpt_port_test(lpt, data, mask))
			{ status = 0 ; goto end_probe ; }
	}

	for (i = 0; i < 8; i++)	{		/* Walking one */
		data = (1 << i);
		if (!nlpt_port_test(lpt, data, mask))
			{ status = 0 ; goto end_probe ; }
	}

end_probe:
	/* write 0's to control and data ports */
	ppb_wdtr(&lpt->lpt_dev, 0);
	ppb_wctr(&lpt->lpt_dev, 0);

	ppb_release_bus(&lpt->lpt_dev);

	return (status);
}

/*
 * nlptprobe()
 */
static struct ppb_device *
nlptprobe(struct ppb_data *ppb)
{
	struct lpt_data *lpt;

	lpt = (struct lpt_data *) malloc(sizeof(struct lpt_data),
							M_TEMP, M_NOWAIT);
	if (!lpt) {
		printf("nlpt: cannot malloc!\n");
		return (0);
	}
	bzero(lpt, sizeof(struct lpt_data));

	lptdata[nlpt] = lpt;

	/*
	 * lpt dependent initialisation.
	 */
	lpt->lpt_unit = nlpt;

	/*
	 * ppbus dependent initialisation.
	 */
	lpt->lpt_dev.id_unit = lpt->lpt_unit;
	lpt->lpt_dev.ppb = ppb;
	lpt->lpt_dev.intr = nlptintr;

	/*
	 * Now, try to detect the printer.
	 */
	if (!nlpt_detect(lpt)) {
		free(lpt, M_TEMP);
		return (0);
	}

	/* Ok, go to next device on next probe */
	nlpt ++;

	return (&lpt->lpt_dev);
}

static int
nlptattach(struct ppb_device *dev)
{
	struct lpt_data *lpt = lptdata[dev->id_unit];

	/*
	 * Report ourselves
	 */
	printf("nlpt%d: <generic printer> on ppbus %d\n",
	       dev->id_unit, dev->ppb->ppb_link->adapter_unit);

	lpt->sc_primed = 0;	/* not primed yet */

	if (ppb_request_bus(&lpt->lpt_dev, PPB_DONTWAIT)) {
		printf("nlpt: cannot alloc ppbus!\n");
		return (0);
	}

	ppb_wctr(&lpt->lpt_dev, LPC_NINIT);

	/* check if we can use interrupt, should be done by ppc stuff */
	nlprintf("oldirq %x\n", lpt->sc_irq);
	if (ppb_get_irq(&lpt->lpt_dev)) {
		lpt->sc_irq = LP_HAS_IRQ | LP_USE_IRQ | LP_ENABLE_IRQ;
		printf("nlpt%d: Interrupt-driven port\n", dev->id_unit);
	} else {
		lpt->sc_irq = 0;
		nlprintf("nlpt%d: Polled port\n", dev->id_unit);
	}
	nlprintf("irq %x\n", lpt->sc_irq);

	ppb_release_bus(&lpt->lpt_dev);

	return (1);
}

static void
nlptout(struct lpt_data *lpt)
{	int pl;

	nlprintf ("T %x ", ppb_rstr(&lpt->lpt_dev));
	if (lpt->sc_state & OPEN) {
		lpt->sc_backoff++;
		if (lpt->sc_backoff > hz/LPTOUTMAX)
			lpt->sc_backoff = lpt->sc_backoff > hz/LPTOUTMAX;
		timeout((timeout_func_t)nlptout, (caddr_t)lpt, lpt->sc_backoff);
	} else
		lpt->sc_state &= ~TOUT;

	if (lpt->sc_state & EERROR)
		lpt->sc_state &= ~EERROR;

	/*
	 * Avoid possible hangs do to missed interrupts
	 */
	if (lpt->sc_xfercnt) {
		pl = spltty();
		nlptintr(lpt->lpt_unit);
		splx(pl);
	} else {
		lpt->sc_state &= ~OBUSY;
		wakeup((caddr_t)lpt);
	}

	ppb_release_bus(&lpt->lpt_dev);
}

/*
 * nlptopen -- reset the printer, then wait until it's selected and not busy.
 *	If LP_BYPASS flag is selected, then we do not try to select the
 *	printer -- this is just used for passing ioctls.
 */

static	int
nlptopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct lpt_data *lpt;

	int s;
	int trys;
	u_int unit = LPTUNIT(minor(dev));

	if ((unit >= nlpt))
		return (ENXIO);

	lpt = lptdata[unit];

	if (ppb_request_bus(&lpt->lpt_dev, PPB_WAIT|PPB_INTR))
		return (EINTR);

	if (lpt->sc_state) {
		nlprintf("nlpt: still open %x\n", lpt->sc_state);
		return(EBUSY);
	} else
		lpt->sc_state |= INIT;

	lpt->sc_flags = LPTFLAGS(minor(dev));

	/* Check for open with BYPASS flag set. */
	if (lpt->sc_flags & LP_BYPASS) {
		lpt->sc_state = OPEN;
		return(0);
	}

	s = spltty();
	nlprintf("nlpt flags 0x%x\n", lpt->sc_flags);

	/* set IRQ status according to ENABLE_IRQ flag */
	if (lpt->sc_irq & LP_ENABLE_IRQ)
		lpt->sc_irq |= LP_USE_IRQ;
	else
		lpt->sc_irq &= ~LP_USE_IRQ;

	/* init printer */
	if ((lpt->sc_flags & LP_NO_PRIME) == 0) {
		if((lpt->sc_flags & LP_PRIMEOPEN) || lpt->sc_primed == 0) {
			ppb_wctr(&lpt->lpt_dev, 0);
			lpt->sc_primed++;
			DELAY(500);
		}
	}

	ppb_wctr(&lpt->lpt_dev, LPC_SEL|LPC_NINIT);

	/* wait till ready (printer running diagnostics) */
	trys = 0;
	do {
		/* ran out of waiting for the printer */
		if (trys++ >= LPINITRDY*4) {
			splx(s);
			lpt->sc_state = 0;
			nlprintf ("status %x\n", ppb_rstr(&lpt->lpt_dev) );

			ppb_release_bus(&lpt->lpt_dev);
			return (EBUSY);
		}

		/* wait 1/4 second, give up if we get a signal */
		if (tsleep((caddr_t)lpt, LPPRI|PCATCH, "lptinit", hz/4) !=
		    EWOULDBLOCK) {
			lpt->sc_state = 0;
			splx(s);

			ppb_release_bus(&lpt->lpt_dev);
			return (EBUSY);
		}

		/* is printer online and ready for output */
	} while ((ppb_rstr(&lpt->lpt_dev) &
			(LPS_SEL|LPS_OUT|LPS_NBSY|LPS_NERR)) !=
					(LPS_SEL|LPS_NBSY|LPS_NERR));

	lpt->sc_control = LPC_SEL|LPC_NINIT;
	if (lpt->sc_flags & LP_AUTOLF)
		lpt->sc_control |= LPC_AUTOL;

	/* enable interrupt if interrupt-driven */
	if (lpt->sc_irq & LP_USE_IRQ)
		lpt->sc_control |= LPC_ENA;

	ppb_wctr(&lpt->lpt_dev, lpt->sc_control);

	lpt->sc_state = OPEN;
	lpt->sc_inbuf = geteblk(BUFSIZE);
	lpt->sc_xfercnt = 0;
	splx(s);

	/* only use timeout if using interrupt */
	nlprintf("irq %x\n", lpt->sc_irq);
	if (lpt->sc_irq & LP_USE_IRQ) {
		lpt->sc_state |= TOUT;
		timeout((timeout_func_t)nlptout, (caddr_t)lpt,
			 (lpt->sc_backoff = hz/LPTOUTINITIAL));
	} else
		ppb_release_bus(&lpt->lpt_dev);

	nlprintf("opened.\n");
	return(0);
}

/*
 * nlptclose -- close the device, free the local line buffer.
 *
 * Check for interrupted write call added.
 */

static	int
nlptclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct lpt_data *lpt = lptdata[LPTUNIT(minor(dev))];

	if(lpt->sc_flags & LP_BYPASS)
		goto end_close;

	if (ppb_request_bus(&lpt->lpt_dev, PPB_WAIT|PPB_INTR))
		return (EINTR);

	lpt->sc_state &= ~OPEN;

	/* if the last write was interrupted, don't complete it */
	if((!(lpt->sc_state  & INTERRUPTED)) && (lpt->sc_irq & LP_USE_IRQ))
		while ((ppb_rstr(&lpt->lpt_dev) &
			(LPS_SEL|LPS_OUT|LPS_NBSY|LPS_NERR)) !=
			(LPS_SEL|LPS_NBSY|LPS_NERR) || lpt->sc_xfercnt)
			/* wait 1/4 second, give up if we get a signal */
			if (tsleep((caddr_t)lpt, LPPRI|PCATCH,
				"lpclose", hz) != EWOULDBLOCK)
				break;

	ppb_wctr(&lpt->lpt_dev, LPC_NINIT);
	brelse(lpt->sc_inbuf);

	ppb_release_bus(&lpt->lpt_dev);

end_close:
	lpt->sc_state = 0;
	lpt->sc_xfercnt = 0;
	nlprintf("closed.\n");
	return(0);
}

/*
 * nlpt_pushbytes()
 *	Workhorse for actually spinning and writing bytes to printer
 *	Derived from lpa.c
 *	Originally by ?
 *
 *	This code is only used when we are polling the port
 */
static int
nlpt_pushbytes(struct lpt_data *lpt)
{
	int spin, err, tic;
	char ch;

	nlprintf("p");
	/* loop for every character .. */
	while (lpt->sc_xfercnt > 0) {
		/* printer data */
		ch = *(lpt->sc_cp);
		lpt->sc_cp++;
		lpt->sc_xfercnt--;

		/*
		 * Wait for printer ready.
		 * Loop 20 usecs testing BUSY bit, then sleep
		 * for exponentially increasing timeout. (vak)
		 */
		for (spin = 0; NOT_READY(lpt) && spin < MAX_SPIN; ++spin)
			DELAY(1);	/* XXX delay is NOT this accurate! */
		if (spin >= MAX_SPIN) {
			tic = 0;
			while (NOT_READY(lpt)) {
				/*
				 * Now sleep, every cycle a
				 * little longer ..
				 */
				tic = tic + tic + 1;
				/*
				 * But no more than 10 seconds. (vak)
				 */
				if (tic > MAX_SLEEP)
					tic = MAX_SLEEP;
				err = tsleep((caddr_t)lpt, LPPRI,
					"lptpoll", tic);
				if (err != EWOULDBLOCK) {
					return (err);
				}
			}
		}

		/* output data */
		ppb_wdtr(&lpt->lpt_dev, ch);
		/* strobe */
		ppb_wctr(&lpt->lpt_dev, lpt->sc_control|LPC_STB);
		ppb_wctr(&lpt->lpt_dev, lpt->sc_control);

	}
	return(0);
}

/*
 * nlptwrite --copy a line from user space to a local buffer, then call
 * putc to get the chars moved to the output queue.
 *
 * Flagging of interrupted write added.
 */

static	int
nlptwrite(dev_t dev, struct uio *uio, int ioflag)
{
	register unsigned n;
	int pl, err;
	struct lpt_data *lpt = lptdata[LPTUNIT(minor(dev))];

	if(lpt->sc_flags & LP_BYPASS) {
		/* we can't do writes in bypass mode */
		return(EPERM);
	}

	if (ppb_request_bus(&lpt->lpt_dev, PPB_WAIT|PPB_INTR))
		return (EINTR);

	lpt->sc_state &= ~INTERRUPTED;
	while ((n = min(BUFSIZE, uio->uio_resid)) != 0) {
		lpt->sc_cp = lpt->sc_inbuf->b_un.b_addr ;
		uiomove(lpt->sc_cp, n, uio);
		lpt->sc_xfercnt = n ;
		while ((lpt->sc_xfercnt > 0)&&(lpt->sc_irq & LP_USE_IRQ)) {
			nlprintf("i");
			/* if the printer is ready for a char, */
			/* give it one */
			if ((lpt->sc_state & OBUSY) == 0){
				nlprintf("\nC %d. ", lpt->sc_xfercnt);
				pl = spltty();
				nlptintr(lpt->lpt_unit);
				(void) splx(pl);
			}
			nlprintf("W ");
			if (lpt->sc_state & OBUSY)
				if ((err = tsleep((caddr_t)lpt,
					 LPPRI|PCATCH, "lpwrite", 0))) {
					lpt->sc_state |= INTERRUPTED;
					return(err);
				}
		}
		/* check to see if we must do a polled write */
		if(!(lpt->sc_irq & LP_USE_IRQ) && (lpt->sc_xfercnt)) {
			nlprintf("p");
			if((err = nlpt_pushbytes(lpt)))
				return(err);
		}

		ppb_release_bus(&lpt->lpt_dev);
	}
	return(0);
}

/*
 * nlptintr -- handle printer interrupts which occur when the printer is
 * ready to accept another char.
 *
 * do checking for interrupted write call.
 */

void
nlptintr(int unit)
{
	struct lpt_data *lpt = lptdata[unit];
	int sts;
	int i;

	/*
	 * Is printer online and ready for output?
	 *
	 * Avoid falling back to nlptout() too quickly.  First spin-loop
	 * to see if the printer will become ready ``really soon now''.
	 */
	for (i = 0; i < 100 &&
	     ((sts=ppb_rstr(&lpt->lpt_dev)) & RDY_MASK) != LP_READY; i++) ;

	if ((sts & RDY_MASK) == LP_READY) {
		lpt->sc_state = (lpt->sc_state | OBUSY) & ~EERROR;
		lpt->sc_backoff = hz/LPTOUTINITIAL;

		if (lpt->sc_xfercnt) {
			/* send char */
			/*nlprintf("%x ", *lpt->sc_cp); */
			ppb_wdtr(&lpt->lpt_dev, *lpt->sc_cp++) ;
			ppb_wctr(&lpt->lpt_dev, lpt->sc_control|LPC_STB);
			/* DELAY(X) */
			ppb_wctr(&lpt->lpt_dev, lpt->sc_control);

			/* any more data for printer */
			if(--(lpt->sc_xfercnt) > 0) return;
		}

		/*
		 * No more data waiting for printer.
		 * Wakeup is not done if write call was interrupted.
		 */
		lpt->sc_state &= ~OBUSY;
		if(!(lpt->sc_state & INTERRUPTED))
			wakeup((caddr_t)lpt);
		nlprintf("w ");
		return;
	} else	{	/* check for error */
		if(((sts & (LPS_NERR | LPS_OUT) ) != LPS_NERR) &&
				(lpt->sc_state & OPEN))
			lpt->sc_state |= EERROR;
		/* nlptout() will jump in and try to restart. */
	}
	nlprintf("sts %x ", sts);
}

static	int
nlptioctl(dev_t dev, int cmd, caddr_t data, int flags, struct proc *p)
{
	int	error = 0;
        struct	lpt_data *lpt;
        u_int	unit = LPTUNIT(minor(dev));
	u_char	old_sc_irq;	/* old printer IRQ status */

        lpt = lptdata[unit];

	switch (cmd) {
	case LPT_IRQ :
		if(lpt->sc_irq & LP_HAS_IRQ) {
			/*
			 * NOTE:
			 * If the IRQ status is changed,
			 * this will only be visible on the
			 * next open.
			 *
			 * If interrupt status changes,
			 * this gets syslog'd.
			 */
			old_sc_irq = lpt->sc_irq;
			if(*(int*)data == 0)
				lpt->sc_irq &= (~LP_ENABLE_IRQ);
			else
				lpt->sc_irq |= LP_ENABLE_IRQ;
			if (old_sc_irq != lpt->sc_irq )
				log(LOG_NOTICE, "lpt%c switched to %s mode\n",
					(char)unit+'0',
					(lpt->sc_irq & LP_ENABLE_IRQ)?
					"interrupt-driven":"polled");
		} else /* polled port */
			error = EOPNOTSUPP;
		break;
	default:
		error = ENODEV;
	}

	return(error);
}

static nlpt_devsw_installed = 0;

static void 	nlpt_drvinit(void *unused)
{
	dev_t dev;

	if( ! nlpt_devsw_installed ) {
		dev = makedev(CDEV_MAJOR, 0);
		cdevsw_add(&dev,&nlpt_cdevsw, NULL);
		nlpt_devsw_installed = 1;
    	}
}

SYSINIT(nlptdev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,nlpt_drvinit,NULL)
