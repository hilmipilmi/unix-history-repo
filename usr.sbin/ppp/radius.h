/*
 * Copyright 1999 Internet Business Solutions Ltd., Switzerland
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id:$
 */

struct radius {
  unsigned valid : 1;           /* Is this structure valid ? */
  unsigned vj : 1;              /* FRAMED Compression */
  struct in_addr ip;            /* FRAMED IP */
  struct in_addr mask;          /* FRAMED Netmask */
  unsigned long mtu;            /* FRAMED MTU */
  struct sticky_route *routes;  /* FRAMED Routes */
  struct {
    char file[MAXPATHLEN];	/* Radius config file */
  } cfg;
};

struct bundle;

extern void radius_Init(struct radius *);
extern void radius_Destroy(struct radius *);

extern void radius_Show(struct radius *, struct prompt *);
extern int radius_Authenticate(struct radius *, struct bundle *, const char *,
                               const char *, const char *);
