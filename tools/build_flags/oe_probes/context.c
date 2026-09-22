/* Call-context probe: which call sites does -oe expand?  One tiny callee
   (s1) and one byte extractor (red_of), each called from one context per
   function.  The function name says the context; the runner lists which
   callers still contain a CALL. */
extern int glob;
extern void use(int v);

int s1(int a)
{
    return (glob + a) & 1;
}

unsigned int red_of(unsigned int rgb)
{
    return (rgb >> 16) & 0xff;
}

void c_stmt(int p)    { s1(p); }
void c_assign(int p)  { int x; x = s1(p); use(x); }
void c_expr(int p)    { int x; x = s1(p) & 0xf0; use(x); }
void c_if(int p)      { if (s1(p)) use(1); }
void c_ifeq(int p)    { if (s1(p) == 0) use(1); }
void c_while(int p)   { while (s1(p)) p++; }
void c_arg(int p)     { use(s1(p)); }
void c_ptr(int *q)    { *q = s1(*q); }
void c_lhsand(int p)  { if (s1(p) && p == 1) use(1); }
void c_rhsand(int p)  { if (p == 1 && s1(p)) use(1); }
void c_rhsor(int p)   { if (p == 3 || s1(p)) use(1); }
void c_rhsao(int p, int q) { if ((p == 1 || q == 0x6a) && s1(p)) use(1); }
void c_two(int p)     { int x; x = (s1(p) & 0xf0) | (s1(p + 1) & 0xf0); use(x); }
void c_rgb(unsigned int c)
{
    unsigned int x;
    x = ((red_of(c) & 0xf0u) << 12) | ((red_of(c) & 0xf0u) >> 4);
    use((int) x);
}
