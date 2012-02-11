/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

/*
 * Sun, 31 May 1998 17:50:02 -0600
 * Reported by ToyKeeper <scriven@CS.ColoState.EDU>:
 * For loop-prevention, I know a way to do it which lets most songs play
 * fine once through even if they have backward-jumps. Just keep a small
 * array (256 bytes, or even bits) of flags, each entry determining if a
 * pattern in the song order has been played. If you get to an entry which
 * is already non-zero, skip to the next song (assuming looping is off).
 */

/*
 * Tue, 6 Oct 1998 21:23:17 +0200 (CEST)
 * Reported by John v/d Kamp <blade_@dds.nl>:
 * scan.c was hanging when it jumps to an invalid restart value.
 * (Fixed by hipolito)
 */


#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "effects.h"
#include "mixer.h"

#define S3M_END		0xff
#define S3M_SKIP	0xff
#define MAX_GVL		0x40


int _xmp_scan_module(struct context_data *ctx)
{
    int parm, gvol_slide, f1, f2, p1, p2, ord, ord2;
    int row, last_row, break_row, cnt_row;
    int gvl, bpm, tempo, base_time, chn;
    int alltmp, clock, clock_rst, medbpm;
    int loop_chn, loop_flg;
    int pdelay, skip_fetch;
    int* loop_stk;
    int* loop_row;
    char** tab_cnt;
    struct xmp_event* event;
    struct player_data *p = &ctx->p;
    struct module_data *m = &ctx->m;

    if (m->mod.len == 0)
	return 0;

    medbpm = m->quirk & QUIRK_MEDBPM;

    tab_cnt = calloc (sizeof (char *), m->mod.len);
    for (ord = m->mod.len; ord--;)
	tab_cnt[ord] = calloc(1, m->mod.xxo[ord] >= m->mod.pat ?  1 :
		m->mod.xxp[m->mod.xxo[ord]]->rows ? m->mod.xxp[m->mod.xxo[ord]]->rows : 1);

    loop_stk = calloc(sizeof (int), m->mod.chn);
    loop_row = calloc(sizeof (int), m->mod.chn);
    loop_chn = loop_flg = 0;

    gvl = m->mod.gvl;
    bpm = m->mod.bpm;

    tempo = m->mod.tpo;
    base_time = m->rrate;

    /* By erlk ozlr <erlk.ozlr@gmail.com>
     *
     * xmp doesn't handle really properly the "start" option (-s for the
     * command-line) for DeusEx's .umx files. These .umx files contain
     * several loop "tracks" that never join together. That's how they put
     * multiple musics on each level with a file per level. Each "track"
     * starts at the same order in all files. The problem is that xmp starts
     * decoding the module at order 0 and not at the order specified with
     * the start option. If we have a module that does "0 -> 2 -> 0 -> ...",
     * we cannot play order 1, even with the supposed right option.
     *
     * was: ord2 = ord = -1;
     */
    ord2 = -1;
    ord = ctx->p.start - 1;

    gvol_slide = break_row = cnt_row = alltmp = clock_rst = clock = 0;
    skip_fetch = 0;

    while (42) {
	if ((uint32)++ord >= m->mod.len) {
	    /*if ((uint32)++ord >= m->mod.len)*/
		ord = ((uint32)m->mod.rst > m->mod.len ||
			(uint32)m->mod.xxo[m->mod.rst] >= m->mod.pat) ?
			0 : m->mod.rst;
		//if (m->mod.xxo[ord] == S3M_END)
		 //   break;
	} 

	/* All invalid patterns skipped, only S3M_END aborts replay */
	if ((uint32)m->mod.xxo[ord] >= m->mod.pat) {
	    /*if (m->mod.xxo[ord] == S3M_SKIP) ord++;*/
	    if (m->mod.xxo[ord] == S3M_END) {
		ord = m->mod.len;
	        continue;
	    }
	    continue;
	}

	if (break_row < m->mod.xxp[m->mod.xxo[ord]]->rows && tab_cnt[ord][break_row])
	    break;

	m->xxo_info[ord].gvl = gvl;
	m->xxo_info[ord].bpm = bpm;
	m->xxo_info[ord].tempo = tempo;

	if (medbpm)
	    m->xxo_info[ord].time = (clock + 132 * alltmp / 5 / bpm) / 10;
	else
	    m->xxo_info[ord].time = (clock + 100 * alltmp / bpm) / 10;

	if (!m->xxo_info[ord].start_row && ord) {
	    if (ord == p->start) {
		if (medbpm)
	            clock_rst = clock + 132 * alltmp / 5 / bpm;
		else
		    clock_rst = clock + 100 * alltmp / bpm;
	    }

	    m->xxo_info[ord].start_row = break_row;
	}

	last_row = m->mod.xxp[m->mod.xxo[ord]]->rows;
	for (row = break_row, break_row = 0; row < last_row; row++, cnt_row++) {
	    /* Prevent crashes caused by large softmixer frames */
	    if (bpm < SMIX_MINBPM)
		bpm = SMIX_MINBPM;

	    /* Date: Sat, 8 Sep 2007 04:01:06 +0200
	     * Reported by Zbigniew Luszpinski <zbiggy@o2.pl>
	     * The scan routine falls into infinite looping and doesn't let
	     * xmp play jos-dr4k.xm.
	     * Claudio's workaround: we'll break infinite loops here.
	     *
	     * Date: Oct 27, 2007 8:05 PM
	     * From: Adric Riedel <adric.riedel@gmail.com>
	     * Jesper Kyd: Global Trash 3.mod (the 'Hardwired' theme) only
	     * plays the first 4:41 of what should be a 10 minute piece.
	     * (...) it dies at the end of position 2F
	     */

	    if (cnt_row > 512)	/* was 255, but Global trash goes to 318 */
		goto end_module;

	    if (!loop_flg && tab_cnt[ord][row]) {
		cnt_row--;
		goto end_module;
	    }
	    tab_cnt[ord][row]++;

	    pdelay = 0;

	    for (chn = 0; chn < m->mod.chn; chn++) {
		if (row >= m->mod.xxt[m->mod.xxp[m->mod.xxo[ord]]->index[chn]]->rows)
		    continue;

		event = &EVENT(m->mod.xxo[ord], chn, row);

		/* Pattern delay + pattern break cause target row events
		 * to be ignored
		 */
		if (skip_fetch) {
		    f1 = p1 = f2 = p2 = 0;
		} else {
		    f1 = event->fxt;
		    p1 = event->fxp;
		    f2 = event->f2t;
		    p2 = event->f2p;
		}

		if (f1 == FX_GLOBALVOL || f2 == FX_GLOBALVOL) {
		    gvl = (f1 == FX_GLOBALVOL) ? p1 : p2;
		    gvl = gvl > MAX_GVL ? MAX_GVL : gvl < 0 ? 0 : gvl;
		}

		if (f1 == FX_G_VOLSLIDE || f2 == FX_G_VOLSLIDE) {
		    parm = (f1 == FX_G_VOLSLIDE) ? p1 : p2;
		    if (parm)
			gvol_slide = MSN(parm) - LSN(parm);
		    gvl += gvol_slide * (tempo - !(m->quirk & QUIRK_VSALL));
		}

		if ((f1 == FX_TEMPO && p1) || (f2 == FX_TEMPO && p2)) {
		    parm = (f1 == FX_TEMPO) ? p1 : p2;
		    alltmp += cnt_row * tempo * base_time;
		    cnt_row = 0;
		    if (parm) {
			if (parm <= 0x20)
			    tempo = parm;
			else {
			    if (medbpm)
				clock += 132 * alltmp / 5 / bpm;
			    else
				clock += 100 * alltmp / bpm;

			    alltmp = 0;
			    bpm = parm;
			}
		    }
		}

		if ((f1 == FX_S3M_TEMPO && p1) || (f2 == FX_S3M_TEMPO && p2)) {
		    parm = (f1 == FX_S3M_TEMPO) ? p1 : p2;
		    alltmp += cnt_row * tempo * base_time;
		    cnt_row  = 0;
		    tempo = parm;
		}

		if ((f1 == FX_S3M_BPM && p1) || (f2 == FX_S3M_BPM && p2)) {
		    parm = (f1 == FX_S3M_BPM) ? p1 : p2;
		    alltmp += cnt_row * tempo * base_time;
		    cnt_row = 0;

		    if (medbpm)
			clock += 132 * alltmp / 5 / bpm;
		    else
			clock += 100 * alltmp / bpm;

		    alltmp = 0;
		    bpm = parm;
		}

		if ((f1 == FX_IT_BPM && p1) || (f2 == FX_IT_BPM && p2)) {
		    parm = (f1 == FX_IT_BPM) ? p1 : p2;
		    alltmp += cnt_row * tempo * base_time;
		    cnt_row = 0;
		    clock += 100 * alltmp / bpm;
		    alltmp = 0;

		    if (MSN(parm) == 0) {
			bpm -= LSN(parm);
			if (bpm < 0x20)
			    bpm = 0x20;
		    } else if (MSN(parm) == 1) {
			bpm += LSN(parm);
			if (bpm > 0xff)
			    bpm = 0xff;
		    } else {
			bpm = parm;
		    }
		}

		if (f1 == FX_JUMP || f2 == FX_JUMP) {
		    ord2 = (f1 == FX_JUMP) ? p1 : p2;
		    last_row = 0;
		}

		if (f1 == FX_BREAK || f2 == FX_BREAK) {
		    parm = (f1 == FX_BREAK) ? p1 : p2;
		    break_row = 10 * MSN(parm) + LSN(parm);
		    last_row = 0;
		}

		if (f1 == FX_EXTENDED || f2 == FX_EXTENDED) {
		    parm = (f1 == FX_EXTENDED) ? p1 : p2;

		    if ((parm >> 4) == EX_PATT_DELAY) {
			pdelay = parm & 0x0f;
			alltmp += pdelay * tempo * base_time;
		    }

		    if ((parm >> 4) == EX_PATTERN_LOOP) {
			if (parm &= 0x0f) {
			    if (loop_stk[chn]) {
				if (--loop_stk[chn])
				    loop_chn = chn + 1;
				else {
				    loop_flg--;
				    if (m->quirk & QUIRK_S3MLOOP)
					loop_row[chn] = row + 1;
				}
			    } else {
				if (loop_row[chn] <= row) {
				    loop_stk[chn] = parm;
				    loop_chn = chn + 1;
				    loop_flg++;
				}
			    }
			} else { 
			    loop_row[chn] = row;
			}
		    }
		}
	    }
	    skip_fetch = 0;

	    if (loop_chn) {
		row = loop_row[--loop_chn] - 1;
		loop_chn = 0;
	    }
	}

	if (break_row && pdelay) {
	    skip_fetch = 1;
	}

	if (ord2 >= 0) {
	    ord = --ord2;
	    ord2 = -1;
	}

	alltmp += cnt_row * tempo * base_time;
	cnt_row = 0;
    }
    row = break_row;

end_module:
    p->scan_num = p->start > ord ? 0: tab_cnt[ord][row];
    p->scan_row = row;
    p->scan_ord = ord;

    free(loop_row);
    free(loop_stk);

    for (ord = m->mod.len; ord--; )
	free(tab_cnt[ord]);
    free(tab_cnt);

    clock -= clock_rst;
    alltmp += cnt_row * tempo * base_time;

    if (medbpm)
	return (clock + 132 * alltmp / 5 / bpm) / 10;
    else
	return (clock + 100 * alltmp / bpm) / 10;
}

