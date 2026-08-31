/* Does wcc386 stage a memory operand through EAX before pushing it as a
   call argument, or push the memory operand directly?

   FDPS.LE stages.  Every argument the game passes is MOV EAX,<slot> /
   PUSH EAX -- 000192ee, 000192f2, 000192f6 and 000192fd in
   fdps_battle_compute_unit_gauge_position are four in a row.  The settled
   flag set emits PUSH <slot> instead, which is one byte shorter per
   argument and accounts for the whole size gap between the original body
   and the rebuilt one (0x4d against 0x49 there, and the same per-push gap
   in both of its neighbours in that object).

   Two shapes, because the original does both: arguments taken from frame
   locals and arguments taken from the incoming parameter slots. */

extern void sink(int a, int b, int c, int d);
extern int glob;

void push_locals(void)
{
    int w, x, y, z;

    w = glob + 1;
    x = glob + 2;
    y = glob + 3;
    z = glob + 4;
    sink(w, x, y, z);
}

void push_params(int a, int b, int c, int d)
{
    sink(a, b, c, d);
}
