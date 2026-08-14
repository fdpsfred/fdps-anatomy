/* Data placement probe: separates -mf from -ms.
 *
 * Under -mf the compiler emits const objects and the initialiser images of
 * local aggregates into _TEXT, and copies them to the stack without reloading
 * ES; under -ms both live in DGROUP and every copy is preceded by
 * `mov ax,ss / mov es,ax`.  FDPS.LE matches the -mf shape.
 */

extern void sink_str(const char *);

int local_init(int i)
{
    int tbl[9] = { 50, 200, 500, 800, 1000, 1200, 1300, 1400, 1500 };
    char names[2][20] = { "magic0gg.saf", "magic0bb.saf" };
    sink_str(names[i & 1]);
    return tbl[i % 9];
}

static const int file_const[4] = { 1, 2, 3, 4 };

int read_file_const(int i)
{
    return file_const[i & 3];
}
