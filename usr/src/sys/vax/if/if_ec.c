/*	if_ec.c	6.3	84/03/22	*/

#include "ec.h"

/*
 * 3Com Ethernet Controller interface
 */
#include "../machine/pte.h"

#include "../h/param.h"
#include "../h/systm.h"
#include "../h/mbuf.h"
#include "../h/buf.h"
#include "../h/protosw.h"
#include "../h/socket.h"
#include "../h/vmmac.h"
#include "../h/ioctl.h"
#include "../h/errno.h"

#include "../net/if.h"
#include "../net/netisr.h"
#include "../net/route.h"
#include "../netinet/in.h"
#include "../netinet/in_systm.h"
#include "../netinet/ip.h"
#include "../netinet/ip_var.h"
#include "../netinet/if_ether.h"
#include "../netpup/pup.h"

#include "../vax/cpu.h"
#include "../vax/mtpr.h"
#include "../vaxif/if_ecreg.h"
#include "../vaxif/if_uba.h"
#include "../vaxuba/ubareg.h"
#include "../vaxuba/ubavar.h"

#define	ECMEM	0000000

int	ecprobe(), ecattach(), ecrint(), ecxint(), eccollide();
struct	uba_device *ecinfo[NEC];
u_short ecstd[] = { 0 };
struct	uba_driver ecdriver =
	{ ecprobe, 0, ecattach, 0, ecstd, "ec", ecinfo };
#define	ECUNIT(x)	minor(x)

int	ecinit(),ecioctl(),ecoutput(),ecreset();
struct	mbuf *ecget();

extern struct ifnet loif;

/*
 * Ethernet software status per interface.
 *
 * Each interface is referenced by a network interface structure,
 * es_if, which the routing code uses to locate the interface.
 * This structure contains the output queue for the interface, its address, ...
 * We also have, for each interface, a UBA interface structure, which
 * contains information about the UNIBUS resources held by the interface:
 * map registers, buffered data paths, etc.  Information is cached in this
 * structure for use by the if_uba.c routines in running the interface
 * efficiently.
 */
struct	ec_softc {
	struct	arpcom es_ac;		/* common Ethernet structures */
#define	es_if	es_ac.ac_if		/* network-visible interface */
#define	es_addr	es_ac.ac_enaddr		/* hardware Ethernet address */
	struct	ifuba es_ifuba;		/* UNIBUS resources */
	short	es_mask;		/* mask for current output delay */
	short	es_oactive;		/* is output active? */
	u_char	*es_buf[16];		/* virtual addresses of buffers */
} ec_softc[NEC];

/*
 * Do output DMA to determine interface presence and
 * interrupt vector.  DMA is too short to disturb other hosts.
 */
ecprobe(reg)
	caddr_t reg;
{
	register int br, cvec;		/* r11, r10 value-result */
	register struct ecdevice *addr = (struct ecdevice *)reg;
	register caddr_t ecbuf = (caddr_t) &umem[numuba][ECMEM];

#ifdef lint
	br = 0; cvec = br; br = cvec;
	ecrint(0); ecxint(0); eccollide(0);
#endif
	/*
	 * Make sure memory is turned on
	 */
	addr->ec_rcr = EC_AROM;
	/*
	 * Disable map registers for ec unibus space,
	 * but don't allocate yet.
	 */
	(void) ubamem(numuba, ECMEM, 32*2, 0);
	/*
	 * Check for existence of buffers on Unibus.
	 */
	if (badaddr((caddr_t)ecbuf, 2)) {
	bad1:
		printf("ec: buffer mem not found\n");
	bad2:
		(void) ubamem(numuba, 0, 0, 0);	/* reenable map (780 only) */
		addr->ec_rcr = EC_MDISAB;	/* disable memory */
		return (0);
	}
#if VAX780
	if (cpu == VAX_780 && uba_hd[numuba].uh_uba->uba_sr) {
		uba_hd[numuba].uh_uba->uba_sr = uba_hd[numuba].uh_uba->uba_sr;
		goto bad1;
	}
#endif

	/*
	 * Tell the system that the board has memory here, so it won't
	 * attempt to allocate the addresses later.
	 */
	if (ubamem(numuba, ECMEM, 32*2, 1) == 0) {
		printf("ecprobe: cannot reserve uba addresses\n");
		goto bad2;
	}

	/*
	 * Make a one byte packet in what should be buffer #0.
	 * Submit it for sending.  This whould cause an xmit interrupt.
	 * The xmit interrupt vector is 8 bytes after the receive vector,
	 * so adjust for this before returning.
	 */
	*(u_short *)ecbuf = (u_short) 03777;
	ecbuf[03777] = '\0';
	addr->ec_xcr = EC_XINTEN|EC_XWBN;
	DELAY(100000);
	addr->ec_xcr = EC_XCLR;
	if (cvec > 0 && cvec != 0x200) {
		if (cvec & 04) {	/* collision interrupt */
			cvec -= 04;
			br += 1;		/* rcv is collision + 1 */
		} else {		/* xmit interrupt */
			cvec -= 010;
			br += 2;		/* rcv is xmit + 2 */
		}
	}
	return (1);
}

/*
 * Interface exists: make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.
 */
ecattach(ui)
	struct uba_device *ui;
{
	struct ec_softc *es = &ec_softc[ui->ui_unit];
	register struct ifnet *ifp = &es->es_if;
	register struct ecdevice *addr = (struct ecdevice *)ui->ui_addr;
	struct sockaddr_in *sin;
	int i, j;
	u_char *cp;

	ifp->if_unit = ui->ui_unit;
	ifp->if_name = "ec";
	ifp->if_mtu = ETHERMTU;

	/*
	 * Read the ethernet address off the board, one nibble at a time.
	 */
	addr->ec_xcr = EC_UECLR;
	addr->ec_rcr = EC_AROM;
	cp = (u_char *) &es->es_addr;
#define	NEXTBIT	addr->ec_rcr = EC_AROM|EC_ASTEP; addr->ec_rcr = EC_AROM
	for (i=0; i < sizeof (es->es_addr); i++) {
		*cp = 0;
		for (j=0; j<=4; j+=4) {
			*cp |= ((addr->ec_rcr >> 8) & 0xf) << j;
			NEXTBIT; NEXTBIT; NEXTBIT; NEXTBIT;
		}
		cp++;
	}
	sin = (struct sockaddr_in *)&es->es_if.if_addr;
	sin->sin_family = AF_INET;
	ifp->if_init = ecinit;
	ifp->if_ioctl = ecioctl;
	ifp->if_output = ecoutput;
	ifp->if_reset = ecreset;
	for (i=0; i<16; i++)
		es->es_buf[i] = (u_char *)&umem[ui->ui_ubanum][ECMEM+2048*i];
	if_attach(ifp);
}

/*
 * Reset of interface after UNIBUS reset.
 * If interface is on specified uba, reset its state.
 */
ecreset(unit, uban)
	int unit, uban;
{
	register struct uba_device *ui;

	if (unit >= NEC || (ui = ecinfo[unit]) == 0 || ui->ui_alive == 0 ||
	    ui->ui_ubanum != uban)
		return;
	printf(" ec%d", unit);
	(void) ubamem(uban, ECMEM, 32*2, 0);	/* mr disable (no alloc) */
	ec_softc[unit].es_if.if_flags &= ~IFF_RUNNING;
	ecinit(unit);
}

/*
 * Initialization of interface; clear recorded pending
 * operations, and reinitialize UNIBUS usage.
 */
ecinit(unit)
	int unit;
{
	struct ec_softc *es = &ec_softc[unit];
	struct ecdevice *addr;
	register struct ifnet *ifp = &es->es_if;
	register struct sockaddr_in *sin;
	int i, s;

	sin = (struct sockaddr_in *)&ifp->if_addr;
	if (sin->sin_addr.s_addr == 0)		/* address still unknown */
		return;

	/*
	 * Hang receive buffers and start any pending writes.
	 * Writing into the rcr also makes sure the memory
	 * is turned on.
	 */
	if ((es->es_if.if_flags & IFF_RUNNING) == 0) {
		addr = (struct ecdevice *)ecinfo[unit]->ui_addr;
		s = splimp();
		for (i = ECRHBF; i >= ECRLBF; i--)
			addr->ec_rcr = EC_READ | i;
		es->es_oactive = 0;
		es->es_mask = ~0;
		es->es_if.if_flags |= IFF_UP|IFF_RUNNING;
		if (es->es_if.if_snd.ifq_head)
			ecstart(unit);
		splx(s);
	}
	if_rtinit(&es->es_if, RTF_UP);
	arpwhohas(&es->es_ac, &sin->sin_addr);
}

/*
 * Start or restart output on interface.
 * If interface is already active, then this is a retransmit
 * after a collision, and just restuff registers.
 * If interface is not already active, get another datagram
 * to send off of the interface queue, and map it to the interface
 * before starting the output.
 */
ecstart(dev)
	dev_t dev;
{
        int unit = ECUNIT(dev);
	struct ec_softc *es = &ec_softc[unit];
	struct ecdevice *addr;
	struct mbuf *m;

	if (es->es_oactive)
		goto restart;

	IF_DEQUEUE(&es->es_if.if_snd, m);
	if (m == 0) {
		es->es_oactive = 0;
		return;
	}
	ecput(es->es_buf[ECTBF], m);

restart:
	addr = (struct ecdevice *)ecinfo[unit]->ui_addr;
	addr->ec_xcr = EC_WRITE|ECTBF;
	es->es_oactive = 1;
}

/*
 * Ethernet interface transmitter interrupt.
 * Start another output if more data to send.
 */
ecxint(unit)
	int unit;
{
	register struct ec_softc *es = &ec_softc[unit];
	register struct ecdevice *addr =
		(struct ecdevice *)ecinfo[unit]->ui_addr;

	if (es->es_oactive == 0)
		return;
	if ((addr->ec_xcr&EC_XDONE) == 0 || (addr->ec_xcr&EC_XBN) != ECTBF) {
		printf("ec%d: stray xmit interrupt, xcr=%b\n", unit,
			addr->ec_xcr, EC_XBITS);
		es->es_oactive = 0;
		addr->ec_xcr = EC_XCLR;
		return;
	}
	es->es_if.if_opackets++;
	es->es_oactive = 0;
	es->es_mask = ~0;
	addr->ec_xcr = EC_XCLR;
	if (es->es_if.if_snd.ifq_head)
		ecstart(unit);
}

/*
 * Collision on ethernet interface.  Do exponential
 * backoff, and retransmit.  If have backed off all
 * the way print warning diagnostic, and drop packet.
 */
eccollide(unit)
	int unit;
{
	struct ec_softc *es = &ec_softc[unit];

	es->es_if.if_collisions++;
	if (es->es_oactive)
		ecdocoll(unit);
}

ecdocoll(unit)
	int unit;
{
	register struct ec_softc *es = &ec_softc[unit];
	register struct ecdevice *addr =
	    (struct ecdevice *)ecinfo[unit]->ui_addr;
	register i;
	int delay;

	/*
	 * Es_mask is a 16 bit number with n low zero bits, with
	 * n the number of backoffs.  When es_mask is 0 we have
	 * backed off 16 times, and give up.
	 */
	if (es->es_mask == 0) {
		es->es_if.if_oerrors++;
		printf("ec%d: send error\n", unit);
		/*
		 * Reset interface, then requeue rcv buffers.
		 * Some incoming packets may be lost, but that
		 * can't be helped.
		 */
		addr->ec_xcr = EC_UECLR;
		for (i=ECRHBF; i>=ECRLBF; i--)
			addr->ec_rcr = EC_READ|i;
		/*
		 * Reset and transmit next packet (if any).
		 */
		es->es_oactive = 0;
		es->es_mask = ~0;
		if (es->es_if.if_snd.ifq_head)
			ecstart(unit);
		return;
	}
	/*
	 * Do exponential backoff.  Compute delay based on low bits
	 * of the interval timer.  Then delay for that number of
	 * slot times.  A slot time is 51.2 microseconds (rounded to 51).
	 * This does not take into account the time already used to
	 * process the interrupt.
	 */
	es->es_mask <<= 1;
	delay = mfpr(ICR) &~ es->es_mask;
	DELAY(delay * 51);
	/*
	 * Clear the controller's collision flag, thus enabling retransmit.
	 */
	addr->ec_xcr = EC_CLEAR;
}

/*
 * Ethernet interface receiver interrupt.
 * If input error just drop packet.
 * Otherwise purge input buffered data path and examine 
 * packet to determine type.  If can't determine length
 * from type, then have to drop packet.  Othewise decapsulate
 * packet based on type and pass to type specific higher-level
 * input routine.
 */
ecrint(unit)
	int unit;
{
	struct ecdevice *addr = (struct ecdevice *)ecinfo[unit]->ui_addr;

	while (addr->ec_rcr & EC_RDONE)
		ecread(unit);
}

ecread(unit)
	int unit;
{
	register struct ec_softc *es = &ec_softc[unit];
	struct ecdevice *addr = (struct ecdevice *)ecinfo[unit]->ui_addr;
	register struct ether_header *ec;
    	struct mbuf *m;
	int len, off, resid, ecoff, rbuf;
	register struct ifqueue *inq;
	u_char *ecbuf;

	es->es_if.if_ipackets++;
	rbuf = addr->ec_rcr & EC_RBN;
	if (rbuf < ECRLBF || rbuf > ECRHBF)
		panic("ecrint");
	ecbuf = es->es_buf[rbuf];
	ecoff = *(short *)ecbuf;
	if (ecoff <= ECRDOFF || ecoff > 2046) {
		es->es_if.if_ierrors++;
#ifdef notdef
		if (es->es_if.if_ierrors % 100 == 0)
			printf("ec%d: += 100 input errors\n", unit);
#endif
		goto setup;
	}

	/*
	 * Get input data length.
	 * Get pointer to ethernet header (in input buffer).
	 * Deal with trailer protocol: if type is PUP trailer
	 * get true type from first 16-bit word past data.
	 * Remember that type was trailer by setting off.
	 */
	len = ecoff - ECRDOFF - sizeof (struct ether_header);
	ec = (struct ether_header *)(ecbuf + ECRDOFF);
	ec->ether_type = ntohs((u_short)ec->ether_type);
#define	ecdataaddr(ec, off, type)	((type)(((caddr_t)((ec)+1)+(off))))
	if (ec->ether_type >= ETHERPUP_TRAIL &&
	    ec->ether_type < ETHERPUP_TRAIL+ETHERPUP_NTRAILER) {
		off = (ec->ether_type - ETHERPUP_TRAIL) * 512;
		if (off >= ETHERMTU)
			goto setup;		/* sanity */
		ec->ether_type = ntohs(*ecdataaddr(ec, off, u_short *));
		resid = ntohs(*(ecdataaddr(ec, off+2, u_short *)));
		if (off + resid > len)
			goto setup;		/* sanity */
		len = off + resid;
	} else
		off = 0;
	if (len == 0)
		goto setup;

	/*
	 * Pull packet off interface.  Off is nonzero if packet
	 * has trailing header; ecget will then force this header
	 * information to be at the front, but we still have to drop
	 * the type and length which are at the front of any trailer data.
	 */
	m = ecget(ecbuf, len, off);
	if (m == 0)
		goto setup;
	if (off) {
		m->m_off += 2 * sizeof (u_short);
		m->m_len -= 2 * sizeof (u_short);
	}
	switch (ec->ether_type) {

#ifdef INET
	case ETHERPUP_IPTYPE:
		schednetisr(NETISR_IP);
		inq = &ipintrq;
		break;

	case ETHERPUP_ARPTYPE:
		arpinput(&es->es_ac, m);
		goto setup;
#endif
	default:
		m_freem(m);
		goto setup;
	}

	if (IF_QFULL(inq)) {
		IF_DROP(inq);
		m_freem(m);
		goto setup;
	}
	IF_ENQUEUE(inq, m);

setup:
	/*
	 * Reset for next packet.
	 */
	addr->ec_rcr = EC_READ|EC_RCLR|rbuf;
}

/*
 * Ethernet output routine.
 * Encapsulate a packet of type family for the local net.
 * Use trailer local net encapsulation if enough data in first
 * packet leaves a multiple of 512 bytes of data in remainder.
 * If destination is this address or broadcast, send packet to
 * loop device to kludge around the fact that 3com interfaces can't
 * talk to themselves.
 */
ecoutput(ifp, m0, dst)
	struct ifnet *ifp;
	struct mbuf *m0;
	struct sockaddr *dst;
{
	int type, s, error;
	struct ether_addr edst;
	struct in_addr idst;
	register struct ec_softc *es = &ec_softc[ifp->if_unit];
	register struct mbuf *m = m0;
	register struct ether_header *ec;
	register int off;
	struct mbuf *mcopy = (struct mbuf *)0;

	switch (dst->sa_family) {

#ifdef INET
	case AF_INET:
		idst = ((struct sockaddr_in *)dst)->sin_addr;
		if (!arpresolve(&es->es_ac, m, &idst, &edst))
			return (0);	/* if not yet resolved */
		if (in_lnaof(idst) == INADDR_ANY)
			mcopy = m_copy(m, 0, (int)M_COPYALL);
		off = ntohs((u_short)mtod(m, struct ip *)->ip_len) - m->m_len;
		/* need per host negotiation */
		if ((ifp->if_flags & IFF_NOTRAILERS) == 0)
		if (off > 0 && (off & 0x1ff) == 0 &&
		    m->m_off >= MMINOFF + 2 * sizeof (u_short)) {
			type = ETHERPUP_TRAIL + (off>>9);
			m->m_off -= 2 * sizeof (u_short);
			m->m_len += 2 * sizeof (u_short);
			*mtod(m, u_short *) = ntohs((u_short)ETHERPUP_IPTYPE);
			*(mtod(m, u_short *) + 1) = ntohs((u_short)m->m_len);
			goto gottrailertype;
		}
		type = ETHERPUP_IPTYPE;
		off = 0;
		goto gottype;
#endif

	case AF_UNSPEC:
		ec = (struct ether_header *)dst->sa_data;
		edst = ec->ether_dhost;
		type = ec->ether_type;
		goto gottype;

	default:
		printf("ec%d: can't handle af%d\n", ifp->if_unit,
			dst->sa_family);
		error = EAFNOSUPPORT;
		goto bad;
	}

gottrailertype:
	/*
	 * Packet to be sent as trailer: move first packet
	 * (control information) to end of chain.
	 */
	while (m->m_next)
		m = m->m_next;
	m->m_next = m0;
	m = m0->m_next;
	m0->m_next = 0;
	m0 = m;

gottype:
	/*
	 * Add local net header.  If no space in first mbuf,
	 * allocate another.
	 */
	if (m->m_off > MMAXOFF ||
	    MMINOFF + sizeof (struct ether_header) > m->m_off) {
		m = m_get(M_DONTWAIT, MT_HEADER);
		if (m == 0) {
			error = ENOBUFS;
			goto bad;
		}
		m->m_next = m0;
		m->m_off = MMINOFF;
		m->m_len = sizeof (struct ether_header);
	} else {
		m->m_off -= sizeof (struct ether_header);
		m->m_len += sizeof (struct ether_header);
	}
	ec = mtod(m, struct ether_header *);
	ec->ether_dhost = edst;
	ec->ether_shost = es->es_addr;
	ec->ether_type = htons((u_short)type);

	/*
	 * Queue message on interface, and start output if interface
	 * not yet active.
	 */
	s = splimp();
	if (IF_QFULL(&ifp->if_snd)) {
		IF_DROP(&ifp->if_snd);
		error = ENOBUFS;
		goto qfull;
	}
	IF_ENQUEUE(&ifp->if_snd, m);
	if (es->es_oactive == 0)
		ecstart(ifp->if_unit);
	splx(s);
	return (mcopy ? looutput(&loif, mcopy, dst) : 0);

qfull:
	m0 = m;
	splx(s);
bad:
	m_freem(m0);
	if (mcopy)
		m_freem(mcopy);
	return (error);
}

/*
 * Routine to copy from mbuf chain to transmit
 * buffer in UNIBUS memory.
 * If packet size is less than the minimum legal size,
 * the buffer is expanded.  We probably should zero out the extra
 * bytes for security, but that would slow things down.
 */
ecput(ecbuf, m)
	u_char *ecbuf;
	struct mbuf *m;
{
	register struct mbuf *mp;
	register int off;
	u_char *bp;

	for (off = 2048, mp = m; mp; mp = mp->m_next)
		off -= mp->m_len;
	if (2048 - off < ETHERMIN + sizeof (struct ether_header))
		off = 2048 - ETHERMIN - sizeof (struct ether_header);
	*(u_short *)ecbuf = off;
	bp = (u_char *)(ecbuf + off);
	for (mp = m; mp; mp = mp->m_next) {
		register unsigned len = mp->m_len;
		u_char *mcp;

		if (len == 0)
			continue;
		mcp = mtod(mp, u_char *);
		if ((unsigned)bp & 01) {
			*bp++ = *mcp++;
			len--;
		}
		if (off = (len >> 1)) {
			register u_short *to, *from;

			to = (u_short *)bp;
			from = (u_short *)mcp;
			do
				*to++ = *from++;
			while (--off > 0);
			bp = (u_char *)to,
			mcp = (u_char *)from;
		}
		if (len & 01)
			*bp++ = *mcp++;
	}
	m_freem(m);
}

/*
 * Routine to copy from UNIBUS memory into mbufs.
 * Similar in spirit to if_rubaget.
 *
 * Warning: This makes the fairly safe assumption that
 * mbufs have even lengths.
 */
struct mbuf *
ecget(ecbuf, totlen, off0)
	u_char *ecbuf;
	int totlen, off0;
{
	register struct mbuf *m;
	struct mbuf *top = 0, **mp = &top;
	register int off = off0, len;
	u_char *cp;

	cp = ecbuf + ECRDOFF + sizeof (struct ether_header);
	while (totlen > 0) {
		register int words;
		u_char *mcp;

		MGET(m, M_DONTWAIT, MT_DATA);
		if (m == 0)
			goto bad;
		if (off) {
			len = totlen - off;
			cp = ecbuf + ECRDOFF +
				sizeof (struct ether_header) + off;
		} else
			len = totlen;
		if (len >= CLBYTES) {
			struct mbuf *p;

			MCLGET(p, 1);
			if (p != 0) {
				m->m_len = len = CLBYTES;
				m->m_off = (int)p - (int)m;
			} else {
				m->m_len = len = MIN(MLEN, len);
				m->m_off = MMINOFF;
			}
		} else {
			m->m_len = len = MIN(MLEN, len);
			m->m_off = MMINOFF;
		}
		mcp = mtod(m, u_char *);
		if (words = (len >> 1)) {
			register u_short *to, *from;

			to = (u_short *)mcp;
			from = (u_short *)cp;
			do
				*to++ = *from++;
			while (--words > 0);
			mcp = (u_char *)to;
			cp = (u_char *)from;
		}
		if (len & 01)
			*mcp++ = *cp++;
		*mp = m;
		mp = &m->m_next;
		if (off == 0) {
			totlen -= len;
			continue;
		}
		off += len;
		if (off == totlen) {
			cp = ecbuf + ECRDOFF + sizeof (struct ether_header);
			off = 0;
			totlen = off0;
		}
	}
	return (top);
bad:
	m_freem(top);
	return (0);
}

/*
 * Process an ioctl request.
 */
ecioctl(ifp, cmd, data)
	register struct ifnet *ifp;
	int cmd;
	caddr_t data;
{
	register struct ifreq *ifr = (struct ifreq *)data;
	int s = splimp(), error = 0;

	switch (cmd) {

	case SIOCSIFADDR:
		if (ifp->if_flags & IFF_RUNNING)
			if_rtinit(ifp, -1);	/* delete previous route */
		ecsetaddr(ifp, (struct sockaddr_in *)&ifr->ifr_addr);
		ecinit(ifp->if_unit);
		break;

	default:
		error = EINVAL;
	}
	splx(s);
	return (error);
}

ecsetaddr(ifp, sin)
	register struct ifnet *ifp;
	register struct sockaddr_in *sin;
{

	ifp->if_addr = *(struct sockaddr *)sin;
	ifp->if_net = in_netof(sin->sin_addr);
	ifp->if_host[0] = in_lnaof(sin->sin_addr);
	sin = (struct sockaddr_in *)&ifp->if_broadaddr;
	sin->sin_family = AF_INET;
	sin->sin_addr = if_makeaddr(ifp->if_net, INADDR_ANY);
	ifp->if_flags |= IFF_BROADCAST;
}
