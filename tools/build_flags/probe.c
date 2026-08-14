/* Probe translation unit for reverse-deriving the wcc386 flag set.
 *
 * Every construct here has an observable counterpart in FDPS.LE:
 *   - stack frame shape and argument passing  -> -3s / -3r
 *   - segment override on jump tables         -> -ms / -mf
 *   - placement of const tables and literals  -> -zc
 *   - x87 instruction encoding                -> -fpi / -fpi87 / -fpc
 *   - large frame stack probe                 -> presence of -s
 * The file is compiled under a flag matrix by probe_matrix.py; nothing here is
 * meant to run.
 */

extern int sink(int a, int b, int c, int d, int e);
extern void sink_str(const char *s);
extern void sink_dbl(double d);

/* const table: lands in CONST (DGROUP) or in _TEXT depending on -zc */
static const int const_table[9] = { 50, 200, 500, 800, 1000, 1200, 1300, 1400, 1500 };
static const char const_name[20] = "magic0gg.saf";

/* writable initialised data: always _DATA */
char writable_name[20] = "magic0bb.saf";
char *literal_ptr = "Fight.vfs";

int five_args(int a, int b, int c, int d, int e)
{
    return sink(e, d, c, b, a) + a * b - c;
}

int switch_dispatch(int op, int x)
{
    switch (op) {
    case 0: return x + 1;
    case 1: return x - 1;
    case 2: return x * 3;
    case 3: return x / 5;
    case 4: return x ^ 0x55;
    case 5: return x | 0x0F;
    case 6: return x & 0x7F;
    case 7: return -x;
    }
    return 0;
}

int read_const(int i)
{
    sink_str(const_name);
    sink_str(literal_ptr);
    return const_table[i & 7];
}

int scale_float(int v, float ratio)
{
    double d = (double)v * ratio;
    sink_dbl(d);
    return (int)(d * 1.5);
}

int big_frame(int n)
{
    char buf[8192];
    int i;
    for (i = 0; i < 8192; ++i)
        buf[i] = (char)(i + n);
    return buf[n & 8191];
}

struct packed_probe {
    char  a;
    int   b;
    char  c;
    short d;
    double e;
};

int struct_stride(struct packed_probe *p, int i)
{
    return (int)p[i].b + p[i].a + p[i].d;
}
