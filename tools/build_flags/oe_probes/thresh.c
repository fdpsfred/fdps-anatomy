/* -oe=N threshold probe: callee fNN has NN statements of the same
   shape, each called once as a statement from its own caller cNN.
   The runner reports, for each N, the largest callee still expanded.
   Generated shape; edit by hand only together with oe_probes.py. */
extern int glob;
extern int gv[16];

void f01(int a)
{
    gv[0] = a + glob;
}

void f02(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
}

void f03(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
}

void f04(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
}

void f05(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
}

void f06(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
}

void f07(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
}

void f08(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
}

void f09(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
}

void f10(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
    gv[9] = a + glob;
}

void f11(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
    gv[9] = a + glob;
    gv[10] = a + glob;
}

void f12(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
    gv[9] = a + glob;
    gv[10] = a + glob;
    gv[11] = a + glob;
}

void f13(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
    gv[9] = a + glob;
    gv[10] = a + glob;
    gv[11] = a + glob;
    gv[12] = a + glob;
}

void f14(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
    gv[9] = a + glob;
    gv[10] = a + glob;
    gv[11] = a + glob;
    gv[12] = a + glob;
    gv[13] = a + glob;
}

void f15(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
    gv[9] = a + glob;
    gv[10] = a + glob;
    gv[11] = a + glob;
    gv[12] = a + glob;
    gv[13] = a + glob;
    gv[14] = a + glob;
}

void f16(int a)
{
    gv[0] = a + glob;
    gv[1] = a + glob;
    gv[2] = a + glob;
    gv[3] = a + glob;
    gv[4] = a + glob;
    gv[5] = a + glob;
    gv[6] = a + glob;
    gv[7] = a + glob;
    gv[8] = a + glob;
    gv[9] = a + glob;
    gv[10] = a + glob;
    gv[11] = a + glob;
    gv[12] = a + glob;
    gv[13] = a + glob;
    gv[14] = a + glob;
    gv[15] = a + glob;
}

void c01(int p) { f01(p); }
void c02(int p) { f02(p); }
void c03(int p) { f03(p); }
void c04(int p) { f04(p); }
void c05(int p) { f05(p); }
void c06(int p) { f06(p); }
void c07(int p) { f07(p); }
void c08(int p) { f08(p); }
void c09(int p) { f09(p); }
void c10(int p) { f10(p); }
void c11(int p) { f11(p); }
void c12(int p) { f12(p); }
void c13(int p) { f13(p); }
void c14(int p) { f14(p); }
void c15(int p) { f15(p); }
void c16(int p) { f16(p); }
