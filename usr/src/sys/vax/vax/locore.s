/*	locore.s	4.82	83/06/17	*/

#include "../machine/psl.h"
#include "../machine/pte.h"

#include "../h/errno.h"

#include "../vax/mtpr.h"
#include "../vax/trap.h"
#include "../vax/cpu.h"
#include "../vax/nexus.h"
#include "../vax/cons.h"
#include "../vax/clock.h"
#include "../vaxuba/ubareg.h"

#include "dh.h"
#include "dz.h"
#include "uu.h"
#include "mba.h"

	.set	HIGH,0x1f	# mask for total disable
	.set	MCKVEC,4	# offset into scb of machine check vector
	.set	NBPG,512
	.set	PGSHIFT,9

	.set	NISP,3		# number of interrupt stack pages

/*
 * User structure is UPAGES at top of user space.
 */
	.globl	_u
	.set	_u,0x80000000 - UPAGES*NBPG

/*
 * Restart parameter block
 * This is filled in in machdep.c in startup().
 * It MUST be page aligned.
 * When auto-restart occurs, we run restart() in machdep.c, which
 * takes a core-dump and then cold-starts.
 */ 
	.globl	_rpb
_rpb:
	.space	508
erpb:
	.space	4
	.globl	_intstack
_intstack:
	.space	NISP*NBPG
eintstack:

/*
 * Do a dump.
 * Called by auto-restart.
 * May be called manually.
 */
	.align	2
	.globl	_doadump
_doadump:
	nop; nop				# .word 0x0101
#define	_rpbmap	_Sysmap+8			# scb, UNIvec, rpb, istack*4
	bicl2	$PG_PROT,_rpbmap
	bisl2	$PG_KW,_rpbmap
	tstl	_rpb+RP_FLAG			# dump only once!
	bneq	1f
	incl	_rpb+RP_FLAG
	mtpr	$0,$TBIA
	movl	sp,erpb
	movab	erpb,sp
	mfpr	$PCBB,-(sp)
	mfpr	$MAPEN,-(sp)
	mfpr	$IPL,-(sp)
	mtpr	$0,$MAPEN
	mtpr	$HIGH,$IPL
	pushr	$0x3fff
	calls	$0,_dumpsys
1:
	mfpr	$TXCS,r0
	bitl	$TXCS_RDY,r0
	beql	1b
	mtpr	$TXDB_BOOT,$TXDB
	halt

/*
 * Interrupt vector routines
 */ 
	.globl	_waittime

#define	SCBVEC(name)	.align 2; .globl _X/**/name; _X/**/name
#define	PANIC(msg)	clrl _waittime; pushab 1f; \
			calls $1,_panic; 1: .asciz msg
#define	PRINTF(n,msg)	pushab 1f; calls $n+1,_printf; MSG(msg)
#define	MSG(msg)	.data; 1: .asciz msg; .text
#define	PUSHR		pushr $0x3f
#define	POPR		popr $0x3f

SCBVEC(machcheck):
	PUSHR; pushab 6*4(sp); calls $1,_machinecheck; POPR;
	addl2 (sp)+,sp; rei
SCBVEC(kspnotval):
	PUSHR; PANIC("KSP not valid");
SCBVEC(powfail):
	halt
SCBVEC(chme): SCBVEC(chms): SCBVEC(chmu):
	PUSHR; PANIC("CHM? in kernel");
SCBVEC(stray):
	PUSHR; PRINTF(0, "stray scb interrupt\n"); POPR;
	rei
SCBVEC(nexzvec):
	PUSHR; mfpr $IPL,-(sp); PRINTF(1, "nexus stray intr ipl%x\n"); POPR; rei
SCBVEC(cmrd):
	PUSHR; calls $0,_memerr; POPR; rei
SCBVEC(wtime):
	PUSHR; pushl 6*4(sp); PRINTF(1,"write timeout %x\n"); POPR;
	PANIC("wtimo");

#if NMBA > 0
SCBVEC(mba3int):
	PUSHR; pushl $3; brb 1f
SCBVEC(mba2int):
	PUSHR; pushl $2; brb 1f
SCBVEC(mba1int):
	PUSHR; pushl $1; brb 1f
SCBVEC(mba0int):
	PUSHR; pushl $0
1:	calls $1,_mbintr
	POPR
	incl	_cnt+V_INTR
	rei
#endif

#if VAX780
/*
 * Registers for the uba handling code
 */
#define	rUBANUM	r0
#define	rUBAHD	r1
#define	rUVEC	r3
#define	rUBA	r4
/* r2,r5 are scratch */

SCBVEC(ua3int):
	PUSHR; movl $3,rUBANUM; moval _uba_hd+(3*UH_SIZE),rUBAHD; brb 1f
SCBVEC(ua2int):
	PUSHR; movl $2,rUBANUM; moval _uba_hd+(2*UH_SIZE),rUBAHD; brb 1f
SCBVEC(ua1int):
	PUSHR; movl $1,rUBANUM; moval _uba_hd+(1*UH_SIZE),rUBAHD; brb 1f
SCBVEC(ua0int):
	PUSHR; movl $0,rUBANUM; moval _uba_hd+(0*UH_SIZE),rUBAHD;
1:
	incl	_cnt+V_INTR
	mfpr	$IPL,r2				/* r2 = mfpr(IPL); */
	movl	UH_UBA(rUBAHD),rUBA		/* uba = uhp->uh_uba; */
	movl	UBA_BRRVR-0x14*4(rUBA)[r2],rUVEC
					/* uvec = uba->uba_brrvr[r2-0x14] */
ubanorm:
	bleq	ubaerror 
	addl2	UH_VEC(rUBAHD),rUVEC		/* uvec += uh->uh_vec */
	bicl3	$3,(rUVEC),r1 
	jmp	2(r1)				/* 2 skips ``pushr $0x3f'' */
ubaerror:
	PUSHR; calls $0,_ubaerror; POPR		/* ubaerror r/w's r0-r5 */
	tstl rUVEC; jneq ubanorm		/* rUVEC contains result */
	POPR
	rei
#endif
SCBVEC(cnrint):
	PUSHR; calls $0,_cnrint; POPR; incl _cnt+V_INTR; rei
SCBVEC(cnxint):
	PUSHR; calls $0,_cnxint; POPR; incl _cnt+V_INTR; rei
SCBVEC(hardclock):
	PUSHR
	mtpr $ICCS_RUN|ICCS_IE|ICCS_INT|ICCS_ERR,$ICCS
	pushl 4+6*4(sp); pushl 4+6*4(sp);
	calls $2,_hardclock			# hardclock(pc,psl)
#if NPS > 0
	pushl	4+6*4(sp); pushl 4+6*4(sp);
	calls	$2,_psextsync
#endif
	POPR;
	incl	_cnt+V_INTR		## temp so not to break vmstat -= HZ
	rei
SCBVEC(softclock):
	PUSHR
#if NDZ > 0
	calls	$0,_dztimer
#endif
#if NDH > 0
	calls	$0,_dhtimer
#endif
	pushl	4+6*4(sp); pushl 4+6*4(sp);
	calls	$2,_softclock			# softclock(pc,psl)
	POPR; 
	rei
#include "../net/netisr.h"
	.globl	_netisr
SCBVEC(netintr):
	PUSHR
	bbcc	$NETISR_RAW,_netisr,1f; calls $0,_rawintr; 1:
#ifdef INET
#include "../netinet/in_systm.h"
	bbcc	$NETISR_IP,_netisr,1f; calls $0,_ipintr; 1:
#endif
#ifdef NS
	bbcc	$NETISR_NS,_netisr,1f; calls $0,_nsintr; 1:
#endif
	POPR
	rei
#if defined(VAX750) || defined(VAX730)
SCBVEC(consdin):
	PUSHR;
#if defined(VAX750) && !defined(MRSP)
	jsb	tudma
#endif
	calls $0,_turintr;
	POPR;
	incl _cnt+V_INTR;
	rei
SCBVEC(consdout):
	PUSHR; calls $0,_tuxintr; POPR; incl _cnt+V_INTR; rei
#else
SCBVEC(consdin):
	halt
SCBVEC(consdout):
	halt
#endif

#if NDZ > 0
/*
 * DZ pseudo dma routine:
 *	r0 - controller number
 */
	.align	1
	.globl	dzdma
dzdma:
	mull2	$8*20,r0
	movab	_dzpdma(r0),r3		# pdma structure base
					# for this controller
dzploop:
	movl	r3,r0	
	movl	(r0)+,r1		# device register address
	movzbl	1(r1),r2		# get line number
	bitb	$0x80,r2		# TRDY on?
	beql	dzprei			# no	
	bicb2	$0xf8,r2		# clear garbage bits
	mull2	$20,r2
	addl2	r2,r0			# point at line's pdma structure
	movl	(r0)+,r2		# p_mem
	cmpl	r2,(r0)+		# p_mem < p_end ?
	bgequ	dzpcall			# no, go call dzxint
	movb	(r2)+,6(r1)		# dztbuf = *p_mem++
	movl	r2,-8(r0)
	brb 	dzploop			# check for another line
dzprei:
	POPR
	incl	_cnt+V_PDMA
	rei

dzpcall:
	pushl	r3
	pushl	(r0)+			# push tty address
	calls	$1,*(r0)		# call interrupt rtn
	movl	(sp)+,r3
	brb 	dzploop			# check for another line
#endif

#if NUU > 0 && defined(UUDMA)
/*
 * Pseudo DMA routine for tu58 (on DL11)
 *	r0 - controller number
 */
	.align	1
	.globl	uudma
uudma:
	movl	_uudinfo[r0],r2
	movl	16(r2),r2		# r2 = uuaddr
	mull3	$48,r0,r3
	movab	_uu_softc(r3),r5	# r5 = uuc

	cvtwl	2(r2),r1		# c = uuaddr->rdb
	bbc	$15,r1,1f		# if (c & UUDB_ERROR)
	movl	$13,16(r5)		#	uuc->tu_state = TUC_RCVERR;
	rsb				#	let uurintr handle it
1:
	tstl	4(r5)			# if (uuc->tu_rcnt) {
	beql	1f
	movb	r1,*0(r5)		#	*uuc->tu_rbptr++ = r1
	incl	(r5)
	decl	4(r5)			#	if (--uuc->tu_rcnt)
	beql	2f			#		done
	tstl	(sp)+
	POPR				# 	registers saved in ubglue.s
	rei				# }
2:
	cmpl	16(r5),$8		# if (uuc->tu_state != TUS_GETH)
	beql	2f			# 	let uurintr handle it
1:
	rsb
2:
	mull2	$14,r0			# sizeof(uudata[ctlr]) = 14
	movab	_uudata(r0),r4		# data = &uudata[ctlr];
	cmpb	$1,(r4)			# if (data->pk_flag != TUF_DATA)
	bneq	1b
#ifdef notdef
	/* this is for command packets */
	beql	1f			# 	r0 = uuc->tu_rbptr
	movl	(r5),r0
	brb	2f
1:					# else
#endif
	movl	24(r5),r0		# 	r0 = uuc->tu_addr
2:
	movzbl	1(r4),r3		# counter to r3 (data->pk_count)
	movzwl	(r4),r1			# first word of checksum (=header)
	mfpr	$IPL,-(sp)		# s = spl5();
	mtpr	$0x15,$IPL		# to keep disk interrupts out
	clrw	(r2)			# disable receiver interrupts
3:	bbc	$7,(r2),3b		# while ((uuaddr->rcs & UUCS_READY)==0);
	cvtwb	2(r2),(r0)+		# *buffer = uuaddr->rdb & 0xff
	sobgtr	r3,1f			# continue with next byte ...
	addw2	2(r2),r1		# unless this was the last (odd count)
	brb	2f

1:	bbc	$7,(r2),1b		# while ((uuaddr->rcs & UUCS_READY)==0);
	cvtwb	2(r2),(r0)+		# *buffer = uuaddr->rdb & 0xff
	addw2	-2(r0),r1		# add to checksum..
2:
	adwc	$0,r1			# get the carry
	sobgtr	r3,3b			# loop while r3 > 0
/*
 * We're ready to get the checksum
 */
1:	bbc	$7,(r2),1b		# while ((uuaddr->rcs & UUCS_READY)==0);
	cvtwb	2(r2),12(r4)		# get first (lower) byte
1:	bbc	$7,(r2),1b
	cvtwb	2(r2),13(r4)		# ..and second
	cmpw	12(r4),r1		# is checksum ok?
	beql	1f
	movl	$14,16(r5)		# uuc->tu_state = TUS_CHKERR
	brb	2f			# exit
1:
	movl	$11,16(r5)		# uuc->tu_state = TUS_GET (ok)
2:
	movw	$0x40,(r2)		# enable receiver interrupts
	mtpr	(sp)+,$IPL		# splx(s);
	rsb				# continue processing in uurintr
#endif

#if defined(VAX750) && !defined(MRSP)
/*
 * Pseudo DMA routine for console tu58 
 *   	    (without MRSP)
 */
	.align	1
	.globl	tudma
tudma:
	movab	_tu,r5			# r5 = tu
	tstl	4(r5)			# if (tu.tu_rcnt) {
	beql	3f
	mfpr	$CSRD,r1		# get data from tu58
	movb	r1,*0(r5)		#	*tu.tu_rbptr++ = r1
	incl	(r5)
	decl	4(r5)			#	if (--tu.tu_rcnt)
	beql	1f			#		done
	tstl	(sp)+
	POPR				# 	registers saved in ubglue.s
	rei				# 	data handled, done
1:					# }
	cmpl	16(r5),$8		# if (tu.tu_state != TUS_GETH)
	beql	2f			# 	let turintr handle it
3:
	rsb
2:
	movab	_tudata,r4		# r4 = tudata
	cmpb	$1,(r4)			# if (tudata.pk_flag != TUF_DATA)
	bneq	3b			# 	let turintr handle it
1:					# else
	movl	24(r5),r1		# get buffer pointer to r1
	movzbl	1(r4),r3		# counter to r3
	movzwl	(r4),r0			# first word of checksum (=header)
	mtpr	$0,$CSRS		# disable receiver interrupts
3:
	bsbw	5f			# wait for next byte
	mfpr	$CSRD,r5
	movb	r5,(r1)+		# *buffer = rdb
	sobgtr	r3,1f			# continue with next byte ...
	mfpr	$CSRD,r2		# unless this was the last (odd count)
	brb	2f

1:	bsbw	5f			# wait for next byte
	mfpr	$CSRD,r5
	movb	r5,(r1)+		# *buffer = rdb
	movzwl	-2(r1),r2		# get the last word back from memory
2:
	addw2	r2,r0			# add to checksum..
	adwc	$0,r0			# get the carry
	sobgtr	r3,3b			# loop while r3 > 0
/*
 * We're ready to get the checksum.
 */
	bsbw	5f
	movab	_tudata,r4
	mfpr	$CSRD,r5
	movb	r5,12(r4)		# get first (lower) byte
	bsbw	5f
	mfpr	$CSRD,r5
	movb	r5,13(r4)		# ..and second
	movab	_tu,r5
	cmpw	12(r4),r0		# is checksum ok?
	beql	1f
	movl	$14,16(r5)		# tu.tu_state = TUS_CHKERR
	brb	2f			# exit
1:
	movl	$11,16(r5)		# tu.tu_state = TUS_GET
2:
	mtpr	$0x40,$CSRS		# enable receiver interrupts
	rsb				# continue processing in turintr
/*
 * Loop until a new byte is ready from
 * the tu58, make sure we don't loop forever
 */
5:
	mfpr	$IPL,-(sp)		# can't loop at ipl7, better
	mtpr	$0x15,$IPL		# move down to 5
	movl	$5000,r5		# loop max 5000 times
1:
	mfpr	$CSRS,r2
	bbs	$7,r2,1f
	sobgtr	r5,1b
	movab	_tu,r5
	movl	$13,16(r5)		# return TUS_RCVERR
	mtpr	(sp)+,$IPL
	tstl	(sp)+			# and let turintr handle it
	rsb				# before we go back to turintr
1:
	mtpr	(sp)+,$IPL
	rsb
#endif

/*
 * Stray UNIBUS interrupt catch routines
 */
	.data
	.align	2
#define	PJ	PUSHR;jsb _Xustray
	.globl	_catcher
_catcher:
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ
	PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ;PJ

	.globl	_cold
_cold:	.long	1
	.data

	.text
SCBVEC(ustray):
	blbc	_cold,1f
	mfpr	$IPL,r11
	subl3	$_catcher+8,(sp)+,r10
	ashl	$-1,r10,r10
	POPR
	rei
1:
	subl3	$_catcher+8,(sp)+,r0
	ashl	$-1,r0,-(sp)
	mfpr	$IPL,-(sp)
	PRINTF(2, "uba?: stray intr ipl %x vec %o\n")
	POPR
	rei

/*
 * Trap and fault vector routines
 */ 
#define	TRAP(a)	pushl $T_/**/a; jbr alltraps

/*
 * Ast delivery (profiling and/or reschedule)
 */
SCBVEC(astflt):
	pushl $0; TRAP(ASTFLT)
SCBVEC(privinflt):
	pushl $0; TRAP(PRIVINFLT)
SCBVEC(xfcflt):
	pushl $0; TRAP(XFCFLT)
SCBVEC(resopflt):
	pushl $0; TRAP(RESOPFLT)
SCBVEC(resadflt):
	pushl $0; TRAP(RESADFLT)
SCBVEC(bptflt):
	pushl $0; TRAP(BPTFLT)
SCBVEC(compatflt):
	TRAP(COMPATFLT);
SCBVEC(tracep):
	pushl $0; TRAP(TRCTRAP)
SCBVEC(arithtrap):
	TRAP(ARITHTRAP)
SCBVEC(protflt):
	blbs	(sp)+,segflt
	TRAP(PROTFLT)
segflt:
	TRAP(SEGFLT)
SCBVEC(transflt):
	bitl	$2,(sp)+
	bnequ	tableflt
	jsb	Fastreclaim		# try and avoid pagein
	TRAP(PAGEFLT)
tableflt: 
	TRAP(TABLEFLT)

alltraps:
	mfpr	$USP,-(sp); calls $0,_trap; mtpr (sp)+,$USP
	incl	_cnt+V_TRAP
	addl2	$8,sp			# pop type, code
	mtpr	$HIGH,$IPL		## dont go to a higher IPL (GROT)
	rei

SCBVEC(syscall):
	pushl	$T_SYSCALL
	mfpr	$USP,-(sp); calls $0,_syscall; mtpr (sp)+,$USP
	incl	_cnt+V_SYSCALL
	addl2	$8,sp			# pop type, code
	mtpr	$HIGH,$IPL		## dont go to a higher IPL (GROT)
	rei

/*
 * System page table
 */ 
#define	vaddr(x)	((((x)-_Sysmap)/4)*NBPG+0x80000000)
#define	SYSMAP(mname, vname, npte)			\
_/**/mname:	.globl	_/**/mname;		\
	.space	npte*4;				\
	.globl	_/**/vname;			\
	.set	_/**/vname,vaddr(_/**/mname)

	.data
	.align	2
	SYSMAP(Sysmap	,Sysbase	,SYSPTSIZE	)
	SYSMAP(UMBAbeg	,umbabeg	,0		)
	SYSMAP(Nexmap	,nexus		,16*MAXNNEXUS	)
	SYSMAP(UMEMmap	,umem		,512*MAXNUBA	)
	SYSMAP(UMBAend	,umbaend	,0		)
	SYSMAP(Usrptmap	,usrpt		,USRPTSIZE	)
	SYSMAP(Forkmap	,forkutl	,UPAGES		)
	SYSMAP(Xswapmap	,xswaputl	,UPAGES		)
	SYSMAP(Xswap2map,xswap2utl	,UPAGES		)
	SYSMAP(Swapmap	,swaputl	,UPAGES		)
	SYSMAP(Pushmap	,pushutl	,UPAGES		)
	SYSMAP(Vfmap	,vfutl		,UPAGES		)
	SYSMAP(CMAP1	,CADDR1		,1		)
	SYSMAP(CMAP2	,CADDR2		,1		)
	SYSMAP(mcrmap	,mcr		,1		)
	SYSMAP(mmap	,vmmap		,1		)
	SYSMAP(msgbufmap,msgbuf		,MSGBUFPTECNT	)
	SYSMAP(camap	,cabase		,16*CLSIZE	)
	SYSMAP(ecamap	,calimit	,0		)
	SYSMAP(Mbmap	,mbutl		,NMBCLUSTERS*CLSIZE)

eSysmap:
	.globl	_Syssize
	.set	_Syssize,(eSysmap-_Sysmap)/4
	.text

/*
 * Initialization
 *
 * ipl 0x1f; mapen 0; scbb, pcbb, sbr, slr, isp, ksp not set
 */
	.data
	.globl	_cpu
_cpu:	.long	0
	.text
	.globl	start
start:
	.word	0
/* set system control block base and system page table params */
	mtpr	$_scb-0x80000000,$SCBB
	mtpr	$_Sysmap-0x80000000,$SBR
	mtpr	$_Syssize,$SLR
/* double map the kernel into the virtual user addresses of phys mem */
	mtpr	$_Sysmap,$P0BR
	mtpr	$_Syssize,$P0LR
/* set ISP and get cpu type */
	movl	$_intstack+NISP*NBPG,sp
	mfpr	$SID,r0
	movab	_cpu,r1
	extzv	$24,$8,r0,(r1)
/* init RPB */
	movab	_rpb,r0
	movl	r0,(r0)+			# rp_selfref
	movab	_doadump,r1
	movl	r1,(r0)+			# rp_dumprout
	movl	$0x1f,r2
	clrl	r3
1:	addl2	(r1)+,r3; sobgtr r2,1b
	movl	r3,(r0)+			# rp_chksum
/* count up memory */
	clrl	r7
1:	pushl	$4; pushl r7; calls $2,_badaddr; tstl r0; bneq 9f
	acbl	$8192*1024-1,$64*1024,r7,1b
9:
/* clear memory from kernel bss and pages for proc 0 u. and page table */
	movab	_edata,r6
	movab	_end,r5
	bbcc	$31,r5,0f; 0:
	addl2	$(UPAGES*NBPG)+NBPG+NBPG,r5
1:	clrq	(r6); acbl r5,$8,r6,1b
/* trap() and syscall() save r0-r11 in the entry mask (per ../h/reg.h) */
	bisw2	$0x0fff,_trap
	bisw2	$0x0fff,_syscall
	calls	$0,_fixctlrmask
/* initialize system page table: scb and int stack writeable */
	clrl	r2
	movab	eintstack,r1; bbcc $31,r1,0f; 0: ashl $-PGSHIFT,r1,r1
1:	bisl3	$PG_V|PG_KW,r2,_Sysmap[r2]; aoblss r1,r2,1b
/* make rpb read-only as red zone for interrupt stack */
	bicl2	$PG_PROT,_rpbmap
	bisl2	$PG_KR,_rpbmap
/* make kernel text space read-only */
	movab	_etext+NBPG-1,r1; bbcc $31,r1,0f; 0: ashl $-PGSHIFT,r1,r1
1:	bisl3	$PG_V|PG_KR,r2,_Sysmap[r2]; aoblss r1,r2,1b
/* make kernel data, bss, read-write */
	movab	_end+NBPG-1,r1; bbcc $31,r1,0f; 0:; ashl $-PGSHIFT,r1,r1
1:	bisl3	$PG_V|PG_KW,r2,_Sysmap[r2]; aoblss r1,r2,1b
/* now go to mapped mode */
	mtpr	$1,$TBIA; mtpr $1,$MAPEN; jmp *$0f; 0:
/* init mem sizes */
	ashl	$-PGSHIFT,r7,_maxmem
	movl	_maxmem,_physmem
	movl	_maxmem,_freemem
/* setup context for proc[0] == Scheduler */
	movab	_end+NBPG-1,r6
	bicl2	$NBPG-1,r6		# make page boundary
/* setup page table for proc[0] */
	bbcc	$31,r6,0f; 0:
	ashl	$-PGSHIFT,r6,r3			# r3 = btoc(r6)
	bisl3	$PG_V|PG_KW,r3,_Usrptmap	# init first upt entry
	incl	r3
	movab	_usrpt,r0
	mtpr	r0,$TBIS
/* init p0br, p0lr */
	mtpr	r0,$P0BR
	mtpr	$0,$P0LR
/* init p1br, p1lr */
	movab	NBPG(r0),r0
	movl	$0x200000-UPAGES,r1
	mtpr	r1,$P1LR
	mnegl	r1,r1
	moval	-4*UPAGES(r0)[r1],r2
	mtpr	r2,$P1BR
/* setup mapping for UPAGES of _u */
	movl	$UPAGES,r2; movab _u+NBPG*UPAGES,r1; addl2 $UPAGES,r3; jbr 2f
1:	decl	r3
	moval	-NBPG(r1),r1;
	bisl3	$PG_V|PG_URKW,r3,-(r0)
	mtpr	r1,$TBIS
2:	sobgeq	r2,1b
/* initialize (slightly) the pcb */
	movab	UPAGES*NBPG(r1),PCB_KSP(r1)
	mnegl	$1,PCB_ESP(r1)
	mnegl	$1,PCB_SSP(r1)
	movl	r1,PCB_USP(r1)
	mfpr	$P0BR,PCB_P0BR(r1)
	mfpr	$P0LR,PCB_P0LR(r1)
	movb	$4,PCB_P0LR+3(r1)		# disable ast
	mfpr	$P1BR,PCB_P1BR(r1)
	mfpr	$P1LR,PCB_P1LR(r1)
	movl	$CLSIZE,PCB_SZPT(r1)		# init u.u_pcb.pcb_szpt
	movl	r11,PCB_R11(r1)
	movab	1f,PCB_PC(r1)			# initial pc
	clrl	PCB_PSL(r1)			# mode(k,k), ipl=0
	ashl	$PGSHIFT,r3,r3
	mtpr	r3,$PCBB			# first pcbb
/* set regs, p0br, p0lr, p1br, p1lr, astlvl, ksp and change to kernel mode */
	ldpctx
	rei
/* put signal trampoline code in u. area */
1:	movab	_u,r0
	movc3	$16,sigcode,PCB_SIGC(r0)
/* save reboot flags in global _boothowto */
	movl	r11,_boothowto
/* calculate firstaddr, and call main() */
	movab	_end+NBPG-1,r0; bbcc $31,r0,0f; 0:; ashl $-PGSHIFT,r0,-(sp)
	addl2	$UPAGES+1,(sp); calls $1,_main
/* proc[1] == /etc/init now running here; run icode */
	pushl	$PSL_CURMOD|PSL_PRVMOD; pushl $0; rei

/* signal trampoline code: it is known that this code takes exactly 16 bytes */
/* in ../vax/pcb.h and in the movc3 above */
sigcode:
	calls	$4,5(pc)			# params pushed by sendsig
	chmk	$139				# cleanup mask and onsigstack
	rei
	.word	0x7f				# registers 0-6 (6==sp/compat)
	callg	(ap),*16(ap)
	ret

/*
 * Primitives
 */ 

/*
 * badaddr(addr, len)
 *	see if access addr with a len type instruction causes a machine check
 *	len is length of access (1=byte, 2=short, 4=long)
 */
	.globl	_badaddr
_badaddr:
	.word	0
	movl	$1,r0
	mfpr	$IPL,r1
	mtpr	$HIGH,$IPL
	movl	_scb+MCKVEC,r2
	movl	4(ap),r3
	movl	8(ap),r4
	movab	9f+INTSTK,_scb+MCKVEC
	bbc	$0,r4,1f; tstb	(r3)
1:	bbc	$1,r4,1f; tstw	(r3)
1:	bbc	$2,r4,1f; tstl	(r3)
1:	clrl	r0			# made it w/o machine checks
2:	movl	r2,_scb+MCKVEC
	mtpr	r1,$IPL
	ret
	.align	2
9:
	casel	_cpu,$1,$VAX_MAX
0:
	.word	8f-0b		# 1 is 780
	.word	5f-0b		# 2 is 750
	.word	5f-0b		# 3 is 730
5:
#if defined(VAX750) || defined(VAX730)
	mtpr	$0xf,$MCESR
#endif
	brb	1f
8:
#if VAX780
	mtpr	$0,$SBIFS
#endif
1:
	addl2	(sp)+,sp		# discard mchchk trash
	movab	2b,(sp)
	rei

_addupc:	.globl	_addupc
	.word	0x0
	movl	8(ap),r2		# &u.u_prof
	subl3	8(r2),4(ap),r0		# corrected pc
	blss	9f
	extzv	$1,$31,r0,r0		# logical right shift
	extzv	$1,$31,12(r2),r1	# ditto for scale
	emul	r1,r0,$0,r0
	ashq	$-14,r0,r0
	tstl	r1
	bneq	9f
	bicl2	$1,r0
	cmpl	r0,4(r2)		# length
	bgequ	9f
	addl2	(r2),r0			# base
	probew	$3,$2,(r0)
	beql	8f
	addw2	12(ap),(r0)
9:
	ret
8:
	clrl	12(r2)
	ret

_Copyin:	.globl	_Copyin		# <<<massaged for jsb by asm.sed>>>
	movl	12(sp),r0		# copy length
	blss	ersb
	movl	4(sp),r1		# copy user address
	cmpl	$NBPG,r0		# probing one page or less ?
	bgeq	cishort			# yes
ciloop:
	prober	$3,$NBPG,(r1)		# bytes accessible ?
	beql	ersb			# no
	addl2	$NBPG,r1		# incr user address ptr
	acbl	$NBPG+1,$-NBPG,r0,ciloop	# reduce count and loop
cishort:
	prober	$3,r0,(r1)		# bytes accessible ?
	beql	ersb			# no
	movl	4(sp),r1
	movl	8(sp),r3
	jbr	2f
1:
	subl2	r0,12(sp)
	movc3	r0,(r1),(r3)
2:
	movzwl	$65535,r0
	cmpl	12(sp),r0
	jgtr	1b
	movc3	12(sp),(r1),(r3)
	clrl	r0			#redundant
	rsb

ersb:
	movl	$EFAULT,r0
	rsb

_Copyout: 	.globl	_Copyout	# <<<massaged for jsb by asm.sed >>>
	movl	12(sp),r0		# get count
	blss	ersb
	movl	8(sp),r1		# get user address
	cmpl	$NBPG,r0		# can do in one probew?
	bgeq	coshort			# yes
coloop:
	probew	$3,$NBPG,(r1)		# bytes accessible?
	beql	ersb			# no 
	addl2	$NBPG,r1		# increment user address
	acbl	$NBPG+1,$-NBPG,r0,coloop	# reduce count and loop
coshort:
	probew	$3,r0,(r1)		# bytes accessible?
	beql	ersb			# no
	movl	4(sp),r1
	movl	8(sp),r3
	jbr	2f
1:
	subl2	r0,12(sp)
	movc3	r0,(r1),(r3)
2:
	movzwl	$65535,r0
	cmpl	12(sp),r0
	jgtr	1b
	movc3	12(sp),(r1),(r3)
	clrl	r0				#redundant
	rsb

/*
 * non-local goto's
 */
	.globl	_Setjmp
_Setjmp:
	movq	r6,(r0)+
	movq	r8,(r0)+
	movq	r10,(r0)+
	movq	r12,(r0)+
	addl3	$4,sp,(r0)+
	movl	(sp),(r0)
	clrl	r0
	rsb

	.globl	_Longjmp
_Longjmp:
	movq	(r0)+,r6
	movq	(r0)+,r8
	movq	(r0)+,r10
	movq	(r0)+,r12
	movl	(r0)+,r1
	cmpl	r1,sp				# must be a pop
	bgequ	lj2
	pushab	lj1
	calls	$1,_panic
lj2:
	movl	r1,sp
	jmp	*(r0)				# ``rsb''

lj1:	.asciz	"longjmp"

	.globl	_whichqs
	.globl	_qs
	.globl	_cnt

	.globl	_noproc
	.comm	_noproc,4
	.globl	_runrun
	.comm	_runrun,4

/*
 * The following primitives use the fancy VAX instructions
 * much like VMS does.  _whichqs tells which of the 32 queues _qs
 * have processes in them.  Setrq puts processes into queues, Remrq
 * removes them from queues.  The running process is on no queue,
 * other processes are on a queue related to p->p_pri, divided by 4
 * actually to shrink the 0-127 range of priorities into the 32 available
 * queues.
 */

/*
 * Setrq(p), using fancy VAX instructions.
 *
 * Call should be made at spl6(), and p->p_stat should be SRUN
 */
	.globl	_Setrq		# <<<massaged to jsb by "asm.sed">>>
_Setrq:
	tstl	P_RLINK(r0)		## firewall: p->p_rlink must be 0
	beql	set1			##
	pushab	set3			##
	calls	$1,_panic		##
set1:
	movzbl	P_PRI(r0),r1		# put on queue which is p->p_pri / 4
	ashl	$-2,r1,r1
	movaq	_qs[r1],r2
	insque	(r0),*4(r2)		# at end of queue
	bbss	r1,_whichqs,set2	# mark queue non-empty
set2:
	rsb

set3:	.asciz	"setrq"

/*
 * Remrq(p), using fancy VAX instructions
 *
 * Call should be made at spl6().
 */
	.globl	_Remrq		# <<<massaged to jsb by "asm.sed">>>
_Remrq:
	movzbl	P_PRI(r0),r1
	ashl	$-2,r1,r1
	bbsc	r1,_whichqs,rem1
	pushab	rem3			# it wasn't recorded to be on its q
	calls	$1,_panic
rem1:
	remque	(r0),r2
	beql	rem2
	bbss	r1,_whichqs,rem2
rem2:
	clrl	P_RLINK(r0)		## for firewall checking
	rsb

rem3:	.asciz	"remrq"

/*
 * Masterpaddr is the p->p_addr of the running process on the master
 * processor.  When a multiprocessor system, the slave processors will have
 * an array of slavepaddr's.
 */
	.globl	_masterpaddr
	.data
_masterpaddr:
	.long	0

	.text
sw0:	.asciz	"swtch"
/*
 * Swtch(), using fancy VAX instructions
 */
	.globl	_Swtch
_Swtch:				# <<<massaged to jsb by "asm.sed">>>
	movl	$1,_noproc
	clrl	_runrun
sw1:	ffs	$0,$32,_whichqs,r0	# look for non-empty queue
	bneq	sw1a
	mtpr	$0,$IPL			# must allow interrupts here
	jbr	sw1			# this is an idle loop!
sw1a:	mtpr	$0x18,$IPL		# lock out all so _whichqs==_qs
	bbcc	r0,_whichqs,sw1		# proc moved via lbolt interrupt
	movaq	_qs[r0],r1
	remque	*(r1),r2		# r2 = p = highest pri process
	bvc	sw2			# make sure something was there
sw1b:	pushab	sw0
	calls	$1,_panic
sw2:	beql	sw3
	insv	$1,r0,$1,_whichqs	# still more procs in this queue
sw3:
	clrl	_noproc
	tstl	P_WCHAN(r2)		## firewalls
	bneq	sw1b			##
	movzbl	P_STAT(r2),r3		##
	cmpl	$SRUN,r3		##
	bneq	sw1b			##
	clrl	P_RLINK(r2)		##
	movl	*P_ADDR(r2),r0
	movl	r0,_masterpaddr
	ashl	$PGSHIFT,r0,r0		# r0 = pcbb(p)
/*	mfpr	$PCBB,r1		# resume of current proc is easy
 *	cmpl	r0,r1
 */	beql	res0
	incl	_cnt+V_SWTCH
/* fall into... */

/*
 * Resume(pf)
 */
	.globl	_Resume		# <<<massaged to jsb by "asm.sed">>>
_Resume:
	mtpr	$0x18,$IPL			# no interrupts, please
	movl	_CMAP2,_u+PCB_CMAP2	# yech
	svpctx
	mtpr	r0,$PCBB
	ldpctx
	movl	_u+PCB_CMAP2,_CMAP2	# yech
	mtpr	$_CADDR2,$TBIS
res0:
	tstl	_u+PCB_SSWAP
	beql	res1
	movl	_u+PCB_SSWAP,r0
	clrl	_u+PCB_SSWAP
	movab	_Longjmp,(sp)
	movl	$PSL_PRVMOD,4(sp)		# ``cheating'' (jfr)
res1:
	rei

/*
 * {fu,su},{byte,word}, all massaged by asm.sed to jsb's
 */
	.globl	_Fuword
_Fuword:
	prober	$3,$4,(r0)
	beql	fserr
	movl	(r0),r0
	rsb
fserr:
	mnegl	$1,r0
	rsb

	.globl	_Fubyte
_Fubyte:
	prober	$3,$1,(r0)
	beql	fserr
	movzbl	(r0),r0
	rsb

	.globl	_Suword
_Suword:
	probew	$3,$4,(r0)
	beql	fserr
	movl	r1,(r0)
	clrl	r0
	rsb

	.globl	_Subyte
_Subyte:
	probew	$3,$1,(r0)
	beql	fserr
	movb	r1,(r0)
	clrl	r0
	rsb

/*
 * Copy 1 relocation unit (NBPG bytes)
 * from user virtual address to physical address
 */
_copyseg: 	.globl	_copyseg
	.word	0x0
	bisl3	$PG_V|PG_KW,8(ap),_CMAP2
	mtpr	$_CADDR2,$TBIS	# invalidate entry for copy 
	movc3	$NBPG,*4(ap),_CADDR2
	ret

/*
 * zero out physical memory
 * specified in relocation units (NBPG bytes)
 */
_clearseg: 	.globl	_clearseg
	.word	0x0
	bisl3	$PG_V|PG_KW,4(ap),_CMAP1
	mtpr	$_CADDR1,$TBIS
	movc5	$0,(sp),$0,$NBPG,_CADDR1
	ret

/*
 * Check address.
 * Given virtual address, byte count, and rw flag
 * returns 0 on no access.
 */
_useracc:	.globl	_useracc
	.word	0x0
	movl	4(ap),r0		# get va
	movl	8(ap),r1		# count
	tstl	12(ap)			# test for read access ?
	bneq	userar			# yes
	cmpl	$NBPG,r1			# can we do it in one probe ?
	bgeq	uaw2			# yes
uaw1:
	probew	$3,$NBPG,(r0)
	beql	uaerr			# no access
	addl2	$NBPG,r0
	acbl	$NBPG+1,$-NBPG,r1,uaw1
uaw2:
	probew	$3,r1,(r0)
	beql	uaerr
	movl	$1,r0
	ret

userar:
	cmpl	$NBPG,r1
	bgeq	uar2
uar1:
	prober	$3,$NBPG,(r0)
	beql	uaerr
	addl2	$NBPG,r0
	acbl	$NBPG+1,$-NBPG,r1,uar1
uar2:
	prober	$3,r1,(r0)
	beql	uaerr
	movl	$1,r0
	ret
uaerr:
	clrl	r0
	ret

/*
 * kernacc - check for kernel access privileges
 *
 * We can't use the probe instruction directly because
 * it ors together current and previous mode.
 */
	.globl	_kernacc
_kernacc:
	.word	0x0
	movl	4(ap),r0	# virtual address
	bbcc	$31,r0,kacc1
	bbs	$30,r0,kacerr
	mfpr	$SBR,r2		# address and length of page table (system)
	bbss	$31,r2,0f; 0:
	mfpr	$SLR,r3
	brb	kacc2
kacc1:
	bbsc	$30,r0,kacc3
	mfpr	$P0BR,r2	# user P0
	mfpr	$P0LR,r3
	brb	kacc2
kacc3:
	mfpr	$P1BR,r2	# user P1 (stack)
	mfpr	$P1LR,r3
kacc2:
	addl3	8(ap),r0,r1	# ending virtual address
	addl2	$NBPG-1,r1
	ashl	$-PGSHIFT,r0,r0
	ashl	$-PGSHIFT,r1,r1
	bbs	$31,4(ap),kacc6
	bbc	$30,4(ap),kacc6
	cmpl	r0,r3		# user stack
	blss	kacerr		# address too low
	brb	kacc4
kacc6:
	cmpl	r1,r3		# compare last page to P0LR or SLR
	bgtr	kacerr		# address too high
kacc4:	
	movl	(r2)[r0],r3
	bbc	$31,4(ap),kacc4a
	bbc	$31,r3,kacerr	# valid bit is off
kacc4a:
	cmpzv	$27,$4,r3,$1	# check protection code
	bleq	kacerr		# no access allowed
	tstb	12(ap)
	bneq	kacc5		# only check read access
	cmpzv	$27,$2,r3,$3	# check low 2 bits of prot code
	beql	kacerr		# no write access
kacc5:
	aoblss	r1,r0,kacc4	# next page
	movl	$1,r0		# no errors
	ret
kacerr:
	clrl	r0		# error
	ret
/*
 * Extracted and unrolled most common case of pagein (hopefully):
 *	resident and not on free list (reclaim of page is purely
 *	for the purpose of simulating a reference bit)
 *
 * Built in constants:
 *	CLSIZE of 2, USRSTACK of 0x7ffff000, any bit fields
 *	in pte's or the core map
 */
	.globl	Fastreclaim
	.text
Fastreclaim:
	PUSHR
	extzv	$9,$23,28(sp),r3	# virtual address
	bicl2	$1,r3			# v = clbase(btop(virtaddr)); 
	movl	_u+U_PROCP,r5		# p = u.u_procp 
					# from vtopte(p, v) ...
	cmpl	r3,P_TSIZE(r5)
	jgequ	2f			# if (isatsv(p, v)) {
	ashl	$2,r3,r4
	addl2	P_P0BR(r5),r4		#	tptopte(p, vtotp(p, v));
	movl	$1,r2			#	type = CTEXT;
	jbr	3f
2:
	subl3	P_SSIZE(r5),$0x3ffff8,r0
	cmpl	r3,r0
	jgequ	2f			# } else if (isadsv(p, v)) {
	ashl	$2,r3,r4
	addl2	P_P0BR(r5),r4		#	dptopte(p, vtodp(p, v));
	clrl	r2			#	type = !CTEXT;
	jbr	3f
2:
	cvtwl	P_SZPT(r5),r4		# } else (isassv(p, v)) {
	ashl	$7,r4,r4
	subl2	$(0x3ffff8+UPAGES),r4
	addl2	r3,r4
	ashl	$2,r4,r4
	addl2	P_P0BR(r5),r4		#	sptopte(p, vtosp(p, v));
	clrl	r2			# 	type = !CTEXT;
3:					# }
	bitb	$0x82,3(r4)
	beql	2f			# if (pte->pg_v || pte->pg_fod)
	POPR; rsb			#	let pagein handle it
2:
	bicl3	$0xffe00000,(r4),r0
	jneq	2f			# if (pte->pg_pfnum == 0)
	POPR; rsb			# 	let pagein handle it 
2:
	subl2	_firstfree,r0
	ashl	$-1,r0,r0	
	incl	r0			# pgtocm(pte->pg_pfnum) 
	mull2	$12,r0
	addl2	_cmap,r0		# &cmap[pgtocm(pte->pg_pfnum)] 
	tstl	r2
	jeql	2f			# if (type == CTEXT &&
	jbc	$29,4(r0),2f		#     c_intrans)
	POPR; rsb			# 	let pagein handle it
2:
	jbc	$30,4(r0),2f		# if (c_free)
	POPR; rsb			# 	let pagein handle it 
2:
	bisb2	$0x80,3(r4)		# pte->pg_v = 1;
	jbc	$26,4(r4),2f		# if (anycl(pte, pg_m) 
	bisb2	$0x04,3(r4)		#	pte->pg_m = 1;
2:
	bicw3	$0x7f,2(r4),r0
	bicw3	$0xff80,6(r4),r1
	bisw3	r0,r1,6(r4)		# distcl(pte);
	ashl	$PGSHIFT,r3,r0
	mtpr	r0,$TBIS
	addl2	$NBPG,r0
	mtpr	r0,$TBIS		# tbiscl(v); 
	tstl	r2
	jeql	2f			# if (type == CTEXT) 
	pushl	r4
	pushl	r3
	pushl	P_TEXTP(r5)		#	distpte(p->p_textp,
	calls	$3,_distpte		#	    vtotp(p, v), pte); 
2:					# collect a few statistics...
	incl	_cnt+V_FAULTS		# cnt.v_faults++; 
	incl	_u+U_RU+RU_MINFLT	# u.u_ru.ru_minflt++;
	incl	_cnt+V_PGREC		# cnt.v_pgrec++;
	incl	_cnt+V_FASTPGREC	# cnt.v_fastpgrec++;
	incl	_cnt+V_TRAP		# cnt.v_trap++;
	POPR
	addl2	$8,sp			# pop pc, code
	mtpr	$HIGH,$IPL		## dont go to a higher IPL (GROT)
	rei
