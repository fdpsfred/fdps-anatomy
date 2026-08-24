/* Byte-typed bit tests, the shape that separates 10.0 from 10.0a.
   10.0 folds the memory operand into TEST byte ptr [mem],imm8; 10.0a loads
   the byte first and tests the register.  FDPS.LE's game code has the second
   shape and none of the first, so this is the compiler-side counterpart of
   the library evidence. */
extern unsigned char flags[64];
extern int  sink;
extern void act( void );

void probe_global_flag( void )
{
    if( flags[3] & 0x08 ) act();
}

void probe_indexed_flag( int i )
{
    if( flags[i] & 0x40 ) act();
}

void probe_pointer_flag( unsigned char *p )
{
    if( p[2] & 0x01 ) act();
}

void probe_ternary_flag( int i )
{
    sink = ( flags[i] & 0x20 ) ? 1 : 0;
}

void probe_char_arith( char *s )
{
    sink = s[0] - 'a';
    if( s[1] == 0 ) act();
}
