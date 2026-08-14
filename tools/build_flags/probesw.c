/* Switch-shape probes.
 *
 * FDPS.LE scales its one game-side jump-table index with
 *   lea eax,[eax*4+0]   (8d 04 85 00 00 00 00)
 * while the plain int switch compiles to `shl eax,02H` on every installed
 * Watcom.  Each function here varies one property of the switch so the source
 * construct behind the lea form can be identified.
 */

extern int sink(int);

int sw_int(int op, int x)
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

int sw_unsigned(unsigned op, int x)
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

int sw_base1(int op, int x)
{
    switch (op) {
    case 1: return x + 1;
    case 2: return x - 1;
    case 3: return x * 3;
    case 4: return x / 5;
    case 5: return x ^ 0x55;
    case 6: return x | 0x0F;
    case 7: return x & 0x7F;
    case 8: return -x;
    }
    return 0;
}

int sw_short(short op, int x)
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

int sw_local(int op, int x)
{
    int sel = op;
    int r = 0;
    switch (sel) {
    case 0: r = x + 1; break;
    case 1: r = x - 1; break;
    case 2: r = x * 3; break;
    case 3: r = x / 5; break;
    case 4: r = x ^ 0x55; break;
    case 5: r = x | 0x0F; break;
    case 6: r = x & 0x7F; break;
    case 7: r = -x; break;
    }
    return r;
}

int sw_calls(int op, int x)
{
    switch (op) {
    case 0: return sink(x + 1);
    case 1: return sink(x - 1);
    case 2: return sink(x * 3);
    case 3: return sink(x / 5);
    case 4: return sink(x ^ 0x55);
    case 5: return sink(x | 0x0F);
    case 6: return sink(x & 0x7F);
    case 7: return sink(-x);
    }
    return 0;
}

int sw_many(int op, int x)
{
    switch (op) {
    case 0: return sink(x + 1);
    case 1: return sink(x - 1);
    case 2: return sink(x * 3);
    case 3: return sink(x / 5);
    case 4: return sink(x ^ 0x55);
    case 5: return sink(x | 0x0F);
    case 6: return sink(x & 0x7F);
    case 7: return sink(-x);
    case 8: return sink(x + 2);
    case 9: return sink(x - 2);
    case 10: return sink(x * 5);
    case 11: return sink(x / 7);
    }
    return 0;
}
