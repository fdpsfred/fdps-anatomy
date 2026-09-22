/* Keyword probe: is `__inline` a keyword in Watcom 10.0a C?  The expected
   answer is a syntax error (E1009) at the definition line. */
extern void sink2(int a, int b);

static __inline void inl(int a, int b)
{
    sink2(a, b);
}

void outer(int p, int q)
{
    inl(p, q);
}
