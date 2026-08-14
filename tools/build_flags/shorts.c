/* 16-bit load probe: separates -4s from -5s.
 *
 * -5s replaces every `movsx eax,word ptr X` with `mov eax,dword ptr X-2` plus
 * `sar eax,10H`; -3s and -4s keep MOVSX.  FDPS.LE has 208 MOVSX-word and no
 * SAR-by-16 in the game region, so it is not -5s.
 */

short g_short;
short g_arr[64];

int short_uses(short a, int i)
{
    short loc = (short)(a + 1);
    int r = 0;
    r += a;
    r += loc;
    r += g_short;
    r += g_arr[i & 63];
    return r;
}
