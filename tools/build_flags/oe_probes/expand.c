/* Expansion and push-form probe (no inline keyword anywhere).

   inl() is a plain static function called once from outer(); only -oe can
   expand it.  keep points at it so an out-of-line twin is emitted as well.
   FDPS.LE shows three push forms side by side, and this file has one of each:
     - the expanded copy pushes its parameter-shaped slots directly
       (PUSH dword ptr [EBP-n]),
     - outer's own call stages each argument (MOV EAX,slot / PUSH EAX),
     - the out-of-line twin stages its parameters the same way.
   tcall() is the table-indirect call with a memory argument: FDPS.LE indexes
   it with EDX and stages the argument through EAX (7 sites).  tcall_imm() is
   the same call with an immediate argument, which FDPS.LE indexes with EAX
   (0002e140). */
extern void sink2(int a, int b);
extern int glob;
extern int gidx;

typedef void (*hook_t)(int);
extern hook_t htbl[8];

static void inl(int a, int b)
{
    int t;
    t = a + glob;
    sink2(t, b);
}

void (*keep)(int, int) = inl;

void outer(int p, int q)
{
    int loc;
    loc = p + q;
    inl(loc, q);
    sink2(loc, q);
}

void tcall(int arg)
{
    htbl[gidx](arg);
}

void tcall_imm(void)
{
    htbl[gidx](0);
}
