/***************************************************************************/
/*                               micro-Max,                                */
/* A chess program smaller than 2KB (of non-blank source), by H.G. Muller  */
/***************************************************************************/
/* version 4.8 (1953 characters) features:                                 */
/* - recursive negamax search                                              */
/* - all-capture MVV/LVA quiescence search                                 */
/* - (internal) iterative deepening                                        */
/* - best-move-first 'sorting'                                             */
/* - a hash table storing score and best move                              */
/* - futility pruning                                                      */
/* - king safety through magnetic, frozen king in middle-game              */
/* - R=2 null-move pruning                                                 */
/* - keep hash and repetition-draw detection                               */
/* - better defense against passers through gradual promotion              */
/* - extend check evasions in inner nodes                                  */
/* - reduction of all non-Pawn, non-capture moves except hash move (LMR)   */
/* - full FIDE rules (expt under-promotion) and move-legality checking     */

/* Vendored copy: patched for the Casio BE-300 / Glaze. See UPSTREAM.md
 * for provenance, the pristine SHA-256 and the patch rationale. All
 * deviations from upstream are marked [GLAZE PATCH n]. */

#define W while
/* [GLAZE PATCH 2] Upstream: #define K(A,B) *(int*)(T+A+(B&8)+S*(B&7)).
 * T is a char array, so the offset is rarely 4-byte aligned; an unaligned
 * lw faults with EXC_ADEL, which the Glaze kernel treats as fatal for the
 * process. Load the same little-endian 32-bit value byte by byte instead. */
static int kint(const char *p)
{return (unsigned char)p[0]+((unsigned char)p[1]<<8)+((unsigned char)p[2]<<16)+((int)(unsigned char)p[3]<<24);}
#define K(A,B) kint(T+A+(B&8)+S*(B&7))
#define J(A) K(y+A,b[y])-K(x+A,u)-K(H+A,t)

/* [GLAZE PATCH 1] Upstream: #define U (1<<24), a 192MB BSS hash table.
 * 1<<17 keeps A[] at 1.5MB, which fits the BE-300's 16MB of RAM.
 * (1<<16 would fit an even tighter budget.) */
#define U (1<<17)
struct _ {int K,V;char X,Y,D;} A[U];           /* hash table, 128K entries */

int M=136,S=128,I=8e3,Q,O,K,N,R,J,Z,k=16,*p,c[9]; /* M=0x88                */
int NODE_BUDGET=40000;                       /* [GLAZE PATCH 3] nodes/move */
static int mm_pfrom,mm_pto,mm_pend;             /* [GLAZE PATCH 5] queued human
                                                   move for the next mm_search */

static unsigned s_x=1u;                       /* hash-seed PRNG state      */

char L,
w[]={0,2,2,7,-1,8,12,23},                      /* relative piece values    */
o[]={-16,-15,-17,0,1,16,0,1,16,15,17,0,14,18,31,33,0, /* step-vector lists */
     7,-1,11,6,8,3,6,                          /* 1st dir. in o[] per piece*/
     6,3,5,7,4,5,3,6},                         /* initial piece setup      */
/* [GLAZE PATCH 4] Upstream: b[129] and T[1035], both plain arrays. The
 * J() hash-key macro is evaluated on entry to D() with its index
 * locals (x,y,H,t) uninitialized on purpose; the padding turns every
 * such probe into an in-object access (the probed values were never
 * load-bearing across binaries anyway). bb covers char indices -128..127
 * plus step offsets, tt covers all T windows including 4-byte reads.
 * b and T become pointers into the padded objects; the glue and its
 * externs match. */
bb[128+129+128],*b=bb+128,                    /* board: half of 16x8+dummy*/
tt[128+1024+64],*T=tt+128;                    /* hash translation table   */

int                             /* [GLAZE PATCH 6] upstream defined D() in the
                                   pre-C99 K&R style (implicit int return type,
                                   identifier parameter list); C99 and later
                                   dropped that.  The prototype below is the
                                   same function: the K&R parameters declared
                                   exactly these ints, and D returns int. */
D(int q,int l,int e,int E,int z,int n) /* recursive minimax search, k=moving side, n=depth*/
                                /* (q,l)=window, e=current eval. score, E=e.p. sqr.*/
{                       /* e=score, z=prev.dest; J,Z=hashkeys; return score*/
 int j,r,m,v,d,h,i,F,G,V,P,f=J,g=Z,C,s;
 char t,p,u,x,y,X,Y,H,B;
 struct _*a=A+(J+k*E&U-1);                     /* lookup pos. in hash table*/

 q--;                                          /* adj. window: delay bonus */
 k^=24;                                        /* change sides             */
 d=a->D;m=a->V;X=a->X;Y=a->Y;                  /* resume at stored depth   */
 if(a->K-Z|z|                                  /* miss: other pos. or empty*/
  !(m<=q|X&8&&m>=l|X&S))                       /*   or window incompatible */
  d=Y=0;                                       /* start iter. from scratch */
 X&=~M;                                        /* start at best-move hint  */

 W(d++<n||d<3||                                /* iterative deepening loop */
   z&K==I&&(N<NODE_BUDGET&d<98||               /* [GLAZE PATCH 3] root: deepen upto budget */
   (K=X,L=Y&~M,d=3)))                          /* time's up: go do best    */
 {x=B=X;                                       /* start scan at prev. best */
  h=Y&S;                                       /* request try noncastl. 1st*/
  P=d<3?I:D(-l,1-l,-e,S,0,d-3);                /* Search null move         */
  m=-P<l|R>35?d>2?-I:e:-P;                     /* Prune or stand-pat       */
  N++;                                         /* node count (for timing)  */
  do{u=b[x];                                   /* scan board looking for   */
   if(u&k)                                     /*  own piece (inefficient!)*/
   {r=p=u&7;                                   /* p = piece type (set r>0) */
    j=o[p+16];                                 /* first step vector f.piece*/
    W(r=p>2&r<0?-r:-o[++j])                    /* loop over directions o[] */
    {A:                                        /* resume normal after best */
     y=x;F=G=S;                                /* (x,y)=move, (F,G)=castl.R*/
     do{                                       /* y traverses ray, or:     */
      H=y=h?Y^h:y+r;                           /* sneak in prev. best move */
      if(y&M)break;                            /* board edge hit           */
      m=E-S&b[E]&&y-E<2&E-y<2?I:m;             /* bad castling             */
      if(p<3&y==E)H^=16;                       /* shift capt.sqr. H if e.p.*/
      t=b[H];if(t&k|p<3&!(y-x&7)-!t)break;     /* capt. own, bad pawn mode */
      i=37*w[t&7]+(t&192);                     /* value of capt. piece t   */
      m=i<0?I:m;                               /* K capture                */
      if(m>=l&d>1)goto C;                      /* abort on fail high       */

      v=d-1?e:i-p;                             /* MVV/LVA scoring          */
      if(d-!t>1)                               /* remaining depth          */
      {v=p<6?b[x+8]-b[y+8]:0;                  /* center positional pts.   */
       b[G]=b[H]=b[x]=0;b[y]=u|32;             /* do move, set non-virgin  */
       if(!(G&M))b[F]=k+6,v+=50;               /* castling: put R & score  */
       v-=p-4|R>29?0:20;                       /* penalize mid-game K move */
       if(p<3)                                 /* pawns:                   */
       {v-=9*((x-2&M||b[x-2]-u)+               /* structure, undefended    */
              (x+2&M||b[x+2]-u)-1              /*        squares plus bias */
              +(b[x^16]==k+36))                /* kling to non-virgin King */
              -(R>>2);                         /* end-game Pawn-push bonus */
        V=y+r+1&S?647-p:2*(u&y+16&32);         /* promotion or 6/7th bonus */
        b[y]+=V;i+=V;                          /* change piece, add score  */
       }
       v+=e+i;V=m>q?m:q;                       /* new eval and alpha       */
       J+=J(0);Z+=J(8)+G-S;                    /* update hash key          */
       C=d-1-(d>5&p>2&!t&!h);
       C=R>29|d<3|P-I?C:d;                     /* extend 1 ply if in check */
       do
        s=C>2|v>V?-D(-l,-V,-v,                 /* recursive eval. of reply */
                              F,0,C):v;        /* or fail low if futile    */
       W(s>q&++C<d);v=s;
       if(z&&K-I&&v+I&&x==K&y==L)              /* move pending & in root:  */
       {Q=-e-i;O=F;                            /*   exit if legal & found  */
        a->D=99;a->V=0;                        /* lock game in hash as draw*/
        R+=i>>7;return l;                      /* captured non-P material  */
       }
       J=f;Z=g;                                /* restore hash key         */
       b[G]=k+6;b[F]=b[y]=0;b[x]=u;b[H]=t;     /* undo move,G can be dummy */
      }
      if(v>m)                                  /* new best, update max,best*/
       m=v,X=x,Y=y|S&F;                        /* mark double move with S  */
      if(h){h=0;goto A;}                       /* redo after doing old best*/
      if(x+r-y|u&32|                           /* not 1st step,moved before*/
         p>2&(p-4|j-7||                        /* no P & no lateral K move,*/
         b[G=x+3^r>>1&7]-k-6                   /* no virgin R in corner G, */
         ||b[G^1]|b[G^2])                      /* no 2 empty sq. next to R */
         )t+=p<5;                               /* fake capt. for nonsliding*/
      else F=y;                                /* enable e.p.              */
     }W(!t);                                   /* if not capt. continue ray*/
 }}}W((x=x+9&~M)-B);                          /* next sqr. of board, wrap */
C:if(m>I-M|m<M-I)d=98;                         /* mate holds to any depth  */
  m=m+I|P==I?m:0;                              /* best loses K: (stale)mate*/
  if(a->D<99)                                  /* protect game history     */
   a->K=Z,a->V=m,a->D=d,                       /* always store in hash tab */
   a->X=X|8*(m>q)|S*(m<l),a->Y=Y;              /* move, type (bound/exact),*/
/*if(z)printf("%2d ply, %9d searched, score=%6d by %c%c%c%c\n",d-1,N-S,m,
     'a'+(X&7),'8'-(X>>4),'a'+(Y&7),'8'-(Y>>4&7)); /* uncomment for Kibitz */
 }                                             /*    encoded in X S,8 bits */
 k^=24;                                        /* change sides back        */
 return m+=m<e;                                /* delayed-loss bonus       */
}

/* ------------------------------------------------------------------------
 * [GLAZE PATCH 5] main() replacement.
 *
 * Upstream's main() is a stdio chat-loop: it prints the board with printf,
 * reads moves with getchar(), and reseeds the hash with rand().  The Glaze
 * libc has no getchar()/rand()/board-printing use for us, and the app owns
 * the game flow, so the whole loop is replaced by the small entry points
 * the app links against (documented in packages/chess/engine.h):
 *
 *   mm_init_once()   board setup + hash seeding (once per process)
 *   mm_new_game()    reset for a new game / replay from scratch
 *   mm_set_budget()  nodes per move (difficulty)
 *   mm_apply()       feed one move (legality-checked by the app first)
 *   mm_search()      pick a move for the side to move
 *
 * The setup lines below are byte-for-byte upstream's init except that the
 * center-point table expression is written with integer arithmetic only:
 * upstream computed (K-4)^2+(L-3.5)^2 in float and truncated.  For integer
 * K,L in 0..7 the two agree exactly ((L-3.5)^2 truncates to L*L-7L+12),
 * which keeps libgcc soft-float calls out of the binary.  The table values
 * are identical to upstream's.
 *
 * Upstream passed the search's best move out through the global N (which
 * doubled as the node counter); the local `int N=128` in its old main()
 * shadowed it.  Here the globals K,L carry the move out of D() instead
 * (exactly how 64Logic's UCI core reads it), and N is only the node
 * counter.  N is set to 128 before every D() call so the root always
 * searches at least one full root move (see [GLAZE PATCH 3]).
 * ------------------------------------------------------------------------ */

/* One board of center-point bonuses + start position, as upstream. */
static void mm_setup(void)
{
 K=8;W(K--)
 {b[K]=(b[K+112]=o[K+24]+8)+8;b[K+16]=18;b[K+96]=9;  /* initial board setup*/
  L=8;W(L--)b[16*L+K+8]=(K-4)*(K-4)+L*L-7*L+12;      /* center-pts table   */
 }                                                   /*(in unused half b[])*/
}

void mm_init_once(void)
{
 int i;
 N=1035;                                       /* deterministic hash seed:
                                                  upstream used rand(); we
                                                  fill T[] bytes from our own
                                                  PRNG (T[] stores bytes, so
                                                  byte writes match upstream
                                                  semantics exactly) */
 for(i=0;i<1035;i++) {
  s_x ^= s_x<<13; s_x ^= (unsigned)s_x>>7; s_x ^= s_x<<17;
  T[i]=(char)(s_x>>9);
 }
 L=(char)-1;                                   /* upstream init value      */
 mm_setup();
}

void mm_new_game(void)
{
 static int first=1;
 int i;
 if(!first) for(i=0;i<U;i++) A[i].K=0;         /* clear hash memory        */
 first=0;
 for(i=0;i<128;i++) b[i]=0;
 mm_setup();
 Q=O=R=J=Z=0;                                  /* no ep, no castle rights,
                                                  no ep square, zero hash   */
 k=16;                                         /* white to move: D() flips
                                                   k on entry, so at rest k&24
                                                   is the colour bit of the
                                                   side that just moved (8=
                                                   white,16=black, the engine
                                                   piece colours)            */
 K=I;                                          /* upstream's 'invalid move'
                                                   sentinel; I stays 8e3     */
 L=(char)-1;
 mm_pend=0;                                    /* no human move queued      */
 N=128;
}

void mm_set_budget(int nodes)
{
 NODE_BUDGET=nodes;
}

/* Queue one move the engine did NOT search itself (the human's answer, in
   0x88 coords).  It is not applied here: like upstream main() scanning the
   opponent's move into K,L and letting the next D() call make it, the move
   is applied by the next mm_search as the first half of its call.  The
   engine board therefore only ever advances inside mm_search - keeping it
   in lockstep with the model is the caller's job.  Returns 1 always; the
   queue holds one move, a fresh queue simply replaces an unconsumed one. */
int mm_pending(int from,int to)
{
 mm_pfrom=from; mm_pto=to; mm_pend=1;
 return 1;
}

/* Apply one move straight away, 0x88 coords.  Only for rebuilding the
   engine board from a saved game's history, where every move (both sides')
   must be replayed onto a fresh board.  Returns 1 if the engine accepted
   it (its board actually changed), 0 otherwise; a rejected move leaves the
   engine state untouched, because D() restores everything it changed
   before returning through the normal scan path. */
int mm_apply(int from,int to)
{
 char snap[128];
 int ok,i;
 for(i=0;i<128;i++) snap[i]=b[i];
 K=(char)from; L=(char)to; N=128;
 D(-I,I,Q,O,1,3);                              /* shallow root move check  */
 ok=0;
 for(i=0;i<128;i++) if(b[i]!=snap[i]) {ok=1;break;}
 if(!ok) K=I;                                  /* report invalid           */
 return ok;
}

/* Think for `side` (C_WHITE/C_BLACK: the caller's truth, the model is
   authoritative) and let the engine play the move.  One call mirrors
   upstream main()'s whole play loop:
   1. a queued human move (mm_pending) is applied through a shallow D()
      call, the way upstream hands the scanned-in opponent move to D();
   2. a second D() call searches AND applies the engine's own reply: the
      budget arm of the iterative-deepening loop assigns K,L=best move and
      the final scan applies it through the pending-move exit.
   Returns 1 and stores the reply in the out-params (0x88 coords) only if
   all of: the reply came back, the engine board actually changed (proof
   both applies landed), and the snapshot held a `side` piece on the
   from-square (proof the engine board still agrees with the model; drift
   from an earlier rejected move is caught here instead of at a later
   ply).  On 0 the engine board may have advanced anyway: the caller must
   stop trusting the engine for this game (the model stays authoritative). */
int mm_search(int *from,int *to,int side)
{
 char snap[128];
 int km,ok,i;
 if(mm_pend==1) {                              /* first half: apply the    */
  if(!mm_apply(mm_pfrom,mm_pto)) { mm_pend=0; return 0; } /* queued human move */
  mm_pend=0;
 }
 for(i=0;i<128;i++) snap[i]=b[i];              /* snapshot at the agree     */
 km=k;                                         /*  point, before searching */
 K=I;                                          /* upstream's 'invalid move' */
 L=(char)-1;
 N=128;
 D(-I,I,Q,O,1,3);
 ok=0;
 for(i=0;i<128;i++) if(b[i]!=snap[i]) {ok=1;break;}
 if(!ok) K=I;                                  /* apply path never fired   */
 if(K==I) return 0;
 i=(int)(unsigned char)K;
 if((snap[i]&24)!=(side&24) || (km&(side&24)))
  return 0;   /* wrong piece colour, or k says `side` already moved: drift */

 *from=(unsigned char)K;
 *to=(unsigned char)L;
 return 1;
}

int mm_last_nodes(void)
{
 return N-128;
}
