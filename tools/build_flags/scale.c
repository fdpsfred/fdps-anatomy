/* Index-scaling probe: the one shape no installed Watcom reproduces.
 *
 * FDPS.LE scales an index by the element size with `lea reg,[reg*N+0]` — 304
 * times in the game region, 41 more in the library region — and reserves
 * `shl reg,N` for arithmetic such as expanded constant division.  Every Watcom
 * from 9.5 to 10.6a emits `shl` for all four shapes below.  Each function is a
 * different way of reaching the same multiply, so a version that matches has to
 * match all four.
 */

typedef int (*fn_t)(int);

fn_t ftbl[30];
int gtbl[30];

int via_ptr(int *p, int i)
{
    return p[i];
}

int via_global_tbl(int i)
{
    return gtbl[i];
}

int via_fnptr_tbl(int i, int x)
{
    return ftbl[i](x);
}

int via_ptr_add(int *p, int i, int j)
{
    int *q = p + i;
    return q[j];
}
