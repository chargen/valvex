
/* HOW TO USE

Compile test file (eg test_hello.c) to a .o

It must have an entry point called "entry", which expects to 
take a single argument which is a function pointer (to "serviceFn").

Test file may not reference any other symbols.

*/

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../pub/libvex_basictypes.h"
#include "../pub/libvex_guest_x86.h"
#include "../pub/libvex_guest_amd64.h"
#include "../pub/libvex_guest_ppc32.h"
#include "../pub/libvex.h"
#include "linker.h"

static Int n_bbs_done = 0;


#if defined(__i386__)
#  define VexGuestState             VexGuestX86State
#  define LibVEX_Guest_initialise   LibVEX_GuestX86_initialise
#  define VexArch                   VexArchX86
#  define VexSubArch                VexSubArchX86_sse1
#  define GuestPC                   guest_EIP
#elif defined(__amd64__)
#  define VexGuestState             VexGuestAMD64State
#  define LibVEX_Guest_initialise   LibVEX_GuestAMD64_initialise
#  define VexArch                   VexArchAMD64
#  define VexSubArch                VexSubArch_NONE
#  define GuestPC                   guest_RIP
#elif defined(__powerpc__)
#  define VexGuestState             VexGuestPPC32State
#  define LibVEX_Guest_initialise   LibVEX_GuestPPC32_initialise
#  define VexArch                   VexArchPPC32
#  define VexSubArch                VexSubArchPPC32_noAV
#  define GuestPC                   guest_CIA
#else
#   error "Unknown arch"
#endif

#define TEST_FLAGS (1<<7)|(1<<3)|(1<<2)|(1<<1) //|(1<<0)


/* guest state */
UInt gstack[50000];
VexGuestState gst;
VexControl vcon;

/* only used for the switchback transition */
/* i386:  helper1 = &gst, helper2 = %EFLAGS */
/* amd64: helper1 = &gst, helper2 = %EFLAGS */
HWord sb_helper1 = 0;
HWord sb_helper2 = 0;

/* translation cache */
#define N_TRANS_CACHE 1000000
#define N_TRANS_TABLE 10000

ULong trans_cache[N_TRANS_CACHE];
VexGuestExtents trans_table [N_TRANS_TABLE];
ULong*          trans_tableP[N_TRANS_TABLE];

Int trans_cache_used = 0;
Int trans_table_used = 0;

static Bool chase_into_not_ok ( Addr64 dst ) { return False; }

/* For providing services. */
static HWord serviceFn ( HWord arg1, HWord arg2 )
{
   switch (arg1) {
      case 0: /* EXIT */
         printf("---STOP---\n");
         printf("serviceFn:EXIT\n");
	 printf("%d bbs simulated\n", n_bbs_done);
	 printf("%d translations made, %d tt bytes\n", 
                trans_table_used, 8*trans_cache_used);
         exit(0);
      case 1: /* PUTC */
         putchar(arg2);
         return 0;
      case 2: /* MALLOC */
         return (HWord)malloc(arg2);
      case 3: /* FREE */
         free((void*)arg2);
         return 0;
      default:
         assert(0);
   }
}


/* -------------------- */
/* continue execution on the real CPU (never returns) */
extern void switchback_asm(void);

#if defined(__i386__)

asm(
"switchback_asm:\n"
"   movl sb_helper1, %eax\n"  // eax = guest state ptr
"   movl  16(%eax), %esp\n"   // switch stacks
"   pushl 56(%eax)\n"         // push continuation addr
"   movl sb_helper2, %ebx\n"  // get eflags
"   pushl %ebx\n"             // eflags:CA
"   pushl 0(%eax)\n"          //  EAX:eflags:CA
"   movl 4(%eax), %ecx\n" 
"   movl 8(%eax), %edx\n" 
"   movl 12(%eax), %ebx\n" 
"   movl 20(%eax), %ebp\n"
"   movl 24(%eax), %esi\n"
"   movl 28(%eax), %edi\n"
"   popl %eax\n"
"   popfl\n"
"   ret\n"
);
void switchback ( void )
{
   sb_helper1 = (HWord)&gst;
   sb_helper2 = LibVEX_GuestX86_get_eflags(&gst);
   switchback_asm(); // never returns
}

#elif defined(__amd64__)

asm(
"switchback_asm:\n"
"   movq sb_helper1, %rax\n"  // rax = guest state ptr
"   movq  32(%rax), %rsp\n"   // switch stacks
"   pushq 168(%rax)\n"        // push continuation addr
"   movq sb_helper2, %rbx\n"  // get eflags
"   pushq %rbx\n"             // eflags:CA
"   pushq 0(%rax)\n"          // RAX:eflags:CA
"   movq 8(%rax), %rcx\n" 
"   movq 16(%rax), %rdx\n" 
"   movq 24(%rax), %rbx\n" 
"   movq 40(%rax), %rbp\n"
"   movq 48(%rax), %rsi\n"
"   movq 56(%rax), %rdi\n"

"   movq 64(%rax), %r8\n"
"   movq 72(%rax), %r9\n"
"   movq 80(%rax), %r10\n"
"   movq 88(%rax), %r11\n"
"   movq 96(%rax), %r12\n"
"   movq 104(%rax), %r13\n"
"   movq 112(%rax), %r14\n"
"   movq 120(%rax), %r15\n"

"   popq %rax\n"
"   popfq\n"
"   ret\n"
);
void switchback ( void )
{
   sb_helper1 = (HWord)&gst;
   sb_helper2 = LibVEX_GuestAMD64_get_rflags(&gst);
   switchback_asm(); // never returns
}

#elif defined(__powerpc__)

static void flush_cache(void *ptr, int nbytes)
{
   unsigned long startaddr = (unsigned long) ptr;
   unsigned long endaddr = startaddr + nbytes;
   unsigned long addr;
   unsigned long cls = VG_(cache_line_size);

   startaddr &= ~(cls - 1);
   for (addr = startaddr; addr < endaddr; addr += cls)
      asm volatile("dcbst 0,%0" : : "r" (addr));
   asm volatile("sync");
   for (addr = startaddr; addr < endaddr; addr += cls)
      asm volatile("icbi 0,%0" : : "r" (addr));
   asm volatile("sync; isync");
}


asm(
"switchback_asm:\n"
// SP
"   lis  %r2,sb_helper1\n"     // load hi-wd of guest_state_ptr to r2
"   addi %r2,%r2,sb_helper1\n" // load lo-wd of guest_state_ptr to r2

// LR
//"   mtlr %r2\n"                // move continuation_addr to LR
"   lwz %r4, 412(%r2)\n"       // guest_LR
"   mtlr r4\n"                // move to LR

// CR
"   lis  %r4,sb_helper2\n"     // load hi-wd of flags to r4
"   addi %r4,%r4,sb_helper2\n" // load lo-wd of flags to r4
"   mtcr %r4\n"                // move r4 to CR
"   lwz  %r4, 404(%r2)\n"      // guest_CR0to6
"   mtcrf 0x3F,%r4\n"         // set remaining fields of CR

// CTR
//"   lwz %r4,392(%r2)\n"        // guest_CTR
//"   mtctr %r4\n"               // move r4 to CTR
"   mtctr %r2\n"                // move continuation_addr to CTR

// XER
"   lhz %r4, 412(%r2)\n"       // guest_XER_SO
"   rlwimi $r5,%r4,31,0,0\n"   // rotate and insert to XER[31]
"   lhz %r4, 412(%r2)\n"       // guest_XER_OV
"   rlwimi $r5,%r4,30,1,1\n"   // rotate and insert to XER[30]
"   lhz %r4, 412(%r2)\n"       // guest_XER_CA
"   rlwimi $r5,%r4,29,2,2\n"   // rotate and insert to XER[30]
"   lhz %r4, 412(%r2)\n"       // guest_XER_BC
"   rlwimi $r5,%r4,0,25,31\n"  // rotate and insert to XER[0:6]
"   mtxer %r5\n"               // move r5 to XER

// GPR's
"   lwz %r0,    0(%r2)\n"
"   lwz %r1,    8(%r2)\n"      // switch stacks (r1 = SP)
// r2 not used by vex
"   lwz %r3,   12(%r2)\n"
"   lwz %r4,   16(%r2)\n"
"   lwz %r5,   20(%r2)\n"
"   lwz %r6,   24(%r2)\n"
"   lwz %r7,   28(%r2)\n"
"   lwz %r8,   32(%r2)\n"
"   lwz %r9,   36(%r2)\n"
"   lwz %r10,  40(%r2)\n"
"   lwz %r11,  44(%r2)\n"
"   lwz %r12,  48(%r2)\n"
"   lwz %r13,  52(%r2)\n"
"   lwz %r14,  56(%r2)\n"
"   lwz %r15,  60(%r2)\n"
"   lwz %r16,  64(%r2)\n"
"   lwz %r17,  68(%r2)\n"
"   lwz %r18,  72(%r2)\n"
"   lwz %r19,  76(%r2)\n"
"   lwz %r20,  80(%r2)\n"
"   lwz %r21,  84(%r2)\n"
"   lwz %r22,  88(%r2)\n"
"   lwz %r23,  92(%r2)\n"
"   lwz %r24,  96(%r2)\n"
"   lwz %r25, 100(%r2)\n"
"   lwz %r26, 104(%r2)\n"
"   lwz %r27, 108(%r2)\n"
"   lwz %r28, 112(%r2)\n"
"   lwz %r29, 116(%r2)\n"
"   lwz %r30, 120(%r2)\n"
"   lwz %r31, 124(%r2)\n"
"nop_start_point:\n"
"   nop\n"
"   nop\n"
"   nop\n"
"   nop\n"
"   nop\n"

// Cache Sync - which instr?
/*
dcbst (update memory)
sync (wait for update) 
icbi (invalidate copy in instruction cache) 
isync (perform context synchronization)
eieio ...
*/

"   bctr\n"   // branch to count register
);
extern void nop_start_point;
void switchback ( void )
{
   UInt* p = &nop_start_point;

   Addr32 addr_of_nop = p;
   Addr32 where_to_go = gst->guest_CIA;
   Int    diff = ((Int)addr_of_nop) - ((Int)where_to_go);

   if (diff < -0x2000000 || diff >= 0x2000000) {
     // we're hosed.  Give up
   }

   sb_helper1 = (HWord)&gst;
   sb_helper2 = LibVEX_GuestPPC32_get_flags(&gst);

   /* stay sane ... */
   assert(p[0] == 0 /* whatever the encoding for nop is */);

   p[0] = (0<<31) // no link
     | (0 << 30) // AA=0
     | ((diff >> 2) & 0xFFFFFF)
     | (bits 0 through 5)

   /*
p[0] = "goto ..."
p[1] = gst->guest_CIA
    */

   flush_cache( &p[0], sizeof(UInt) );

   switchback_asm(); // never returns
}

#else
#   error "Unknown arch (switchback)"
#endif

/* -------------------- */
static HWord f, gp, res;
extern void run_translation_asm(void);

#if defined(__i386__)
asm(
"run_translation_asm:\n"
"   pushal\n"
"   movl gp, %ebp\n"
"   movl f, %eax\n"
"   call *%eax\n"
"   movl %eax, res\n"
"   popal\n"
"   ret\n"
);

#elif defined(__amd64__)
asm(
"run_translation_asm:\n"

"   pushq %rax\n"
"   pushq %rbx\n"
"   pushq %rcx\n"
"   pushq %rdx\n"
"   pushq %rbp\n"
"   pushq %rsi\n"
"   pushq %rdi\n"
"   pushq %r8\n"
"   pushq %r9\n"
"   pushq %r10\n"
"   pushq %r11\n"
"   pushq %r12\n"
"   pushq %r13\n"
"   pushq %r14\n"
"   pushq %r15\n"

"   movq gp, %rbp\n"
"   movq f, %rax\n"
"   call *%rax\n"
"   movq %rax, res\n"

"   popq  %r15\n"
"   popq  %r14\n"
"   popq  %r13\n"
"   popq  %r12\n"
"   popq  %r11\n"
"   popq  %r10\n"
"   popq  %r9\n"
"   popq  %r8\n"
"   popq  %rdi\n"
"   popq  %rsi\n"
"   popq  %rbp\n"
"   popq  %rdx\n"
"   popq  %rcx\n"
"   popq  %rbx\n"
"   popq  %rax\n"

"   ret\n"
);

#elif defined(__powerpc__)
asm(
"run_translation_asm:\n"

// CAB: todo

// store registers to stack
// load new stack pointer
// call translation address
// save return value
// reload registers from stack

"   "
);

#else

#   error "Unknown arch"
#endif

void run_translation ( HWord translation )
{
   if (0)
      printf(" run translation %p\n", (void*)translation );
   f = translation;
   gp = (HWord)&gst;
   run_translation_asm();
   gst.GuestPC = res;
   n_bbs_done ++;
}

HWord find_translation ( Addr64 guest_addr )
{
   Int i;
   HWord res;
   if (0)
      printf("find translation %p ... ", ULong_to_Ptr(guest_addr));
   for (i = 0; i < trans_table_used; i++)
     if (trans_table[i].base[0] == guest_addr)
        break;
   if (i == trans_table_used) {
      if (0) printf("none\n");
      return 0; /* not found */
   }
   res = (HWord)trans_tableP[i];
   if (0) printf("%p\n", (void*)res);
   return res;
}

#define N_TRANSBUF 5000
static UChar transbuf[N_TRANSBUF];
void make_translation ( Addr64 guest_addr, Bool verbose )
{
   VexTranslateResult tres;
   Int trans_used, i, ws_needed;
   assert(trans_table_used < N_TRANS_TABLE);
   if (0)
      printf("make translation %p\n", ULong_to_Ptr(guest_addr));
   tres
      = LibVEX_Translate ( 
           VexArch, VexSubArch,
           VexArch, VexSubArch,
           ULong_to_Ptr(guest_addr), guest_addr,
           chase_into_not_ok,
           &trans_table[trans_table_used],
           transbuf, N_TRANSBUF, &trans_used,
           NULL,          /* instrument1 */
           NULL,          /* instrument2 */
           False,         /* cleanup after instrument */
           NULL, /* access checker */
           verbose ? TEST_FLAGS : 0//(1<<3)|(1<<2) //0
        );
   assert(tres == VexTransOK);
   ws_needed = (trans_used+7) / 8;
   assert(ws_needed > 0);
   assert(trans_cache_used + ws_needed < N_TRANS_CACHE);

   for (i = 0; i < trans_used; i++) {
     HChar* dst = ((HChar*)(&trans_cache[trans_cache_used])) + i;
     HChar* src = (HChar*)(&transbuf[i]);
     *dst = *src;
   }

   trans_tableP[trans_table_used] = &trans_cache[trans_cache_used];
   trans_table_used++;
   trans_cache_used += ws_needed;
}


static Int    stopAfter = 0;
static UChar* entry     = NULL;


__attribute__ ((noreturn))
static
void failure_exit ( void )
{
   fprintf(stdout, "VEX did failure_exit.  Bye.\n");
   fprintf(stdout, "bb counter = %d\n\n", n_bbs_done);
   exit(1);
}

static
void log_bytes ( HChar* bytes, Int nbytes )
{
   fwrite ( bytes, 1, nbytes, stdout );
}


/* run simulated code forever (it will exit by calling
   serviceFn(0)). */
static void run_simulator ( void )
{
   static Addr64 last_guest = 0;
   Addr64 next_guest;
   HWord next_host;
   while (1) {
      if (n_bbs_done == stopAfter) {
         printf("---begin SWITCHBACK at %d---\n", n_bbs_done);
	 if (last_guest)
            make_translation(last_guest,True);
         printf("---  end SWITCHBACK at %d---\n", n_bbs_done);
         switchback();
         assert(0); /*NOTREACHED*/
      }

      next_guest = gst.GuestPC;

      if (next_guest == Ptr_to_ULong(&serviceFn)) {
         /* "do" the function call to serviceFn */
#        if defined(__i386__)
         {
            HWord esp = gst.guest_ESP;
            gst.guest_EIP = *(UInt*)(esp+0);
            gst.guest_EAX = serviceFn( *(UInt*)(esp+4), *(UInt*)(esp+8) );
            gst.guest_ESP = esp+4;
            next_guest = gst.guest_EIP;
         }
#        elif defined(__amd64__)
         {
            HWord esp = gst.guest_RSP;
            gst.guest_RIP = *(UInt*)(esp+0);
            gst.guest_RAX = serviceFn( gst.guest_RDI, gst.guest_RSI );
            gst.guest_RSP = esp+8;
            next_guest = gst.guest_RIP;
         }
#        elif defined(__powerpc__)
         {
            HWord esp      = gst.guest_GPR1;
            gst.guest_CIA  = *(UInt*)(esp+0);

// CAB: ?
            gst.guest_GPR2 = serviceFn( *(UInt*)(esp+4), *(UInt*)(esp+8) );

            gst.guest_GPR1 = esp+4;
            next_guest     = gst.guest_CIA;
         }
#        else
#        error "Unknown arch"
#        endif
      }

      next_host = find_translation(next_guest);
      if (next_host == 0) {
         make_translation(next_guest,False);
         next_host = find_translation(next_guest);
	 assert(next_host != 0);
      }
      last_guest = next_guest;
      run_translation(next_host);
   }
}


static void usage ( void )
{
   printf("usage: switchback file.o #bbs\n");
   exit(1);
}

int main ( Int argc, HChar** argv )
{
   HChar* oname;

   struct stat buf;

   if (argc != 3) 
      usage();

   oname = argv[1];
   stopAfter = atoi(argv[2]);

   if (stat(oname, &buf)) {
      printf("switchback: can't stat %s\n", oname);
      return 1;
   }

   entry = linker_top_level_LINK( 1, &argv[1] );

   if (!entry) {
      printf("switchback: can't find entry point\n");
      exit(1);
   }

   LibVEX_default_VexControl(&vcon);
   vcon.guest_max_insns=50;
   vcon.guest_chase_thresh=0;

   LibVEX_Init( failure_exit, log_bytes, 1, False, &vcon );
   LibVEX_Guest_initialise(&gst);

   /* set up as if a call to the entry point passing serviceFn as 
      the one and only parameter */
#  if defined(__i386__)
   gst.guest_EIP = (UInt)entry;
   gst.guest_ESP = (UInt)&gstack[25000];
   *(UInt*)(gst.guest_ESP+4) = (UInt)serviceFn;
   *(UInt*)(gst.guest_ESP+0) = 0x12345678;
#  elif defined(__amd64__)
   gst.guest_RIP = (ULong)entry;
   gst.guest_RSP = (ULong)&gstack[25000];
   gst.guest_RDI = (ULong)serviceFn;
   *(ULong*)(gst.guest_RSP+0) = 0x12345678AABBCCDDULL;
#  elif defined(__powerpc__)
// CAB: ?
   gst.guest_CIA  = (UInt)entry;
   gst.guest_GPR1 = (UInt)&gstack[25000];
   gst.guest_GPR2 = (UInt)serviceFn;
   gst.guest_GPR1 = 0x12345678;
#  else
#  error "Unknown arch"
#  endif

   printf("\n---START---\n");

#if 1
   run_simulator();
#else
   ( (void(*)(HWord(*)(HWord,HWord))) entry ) (serviceFn);
#endif


   return 0;
}