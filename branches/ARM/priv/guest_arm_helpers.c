
/*---------------------------------------------------------------*/
/*---                                                         ---*/
/*--- This file (guest_arm_helpers.c) is                      ---*/
/*--- Copyright (C) OpenWorks LLP.  All rights reserved.      ---*/
/*---                                                         ---*/
/*---------------------------------------------------------------*/

/*
   This file is part of LibVEX, a library for dynamic binary
   instrumentation and translation.

   Copyright (C) 2004-2009 OpenWorks LLP.  All rights reserved.

   This library is made available under a dual licensing scheme.

   If you link LibVEX against other code all of which is itself
   licensed under the GNU General Public License, version 2 dated June
   1991 ("GPL v2"), then you may use LibVEX under the terms of the GPL
   v2, as appearing in the file LICENSE.GPL.  If the file LICENSE.GPL
   is missing, you can obtain a copy of the GPL v2 from the Free
   Software Foundation Inc., 51 Franklin St, Fifth Floor, Boston, MA
   02110-1301, USA.

   For any other uses of LibVEX, you must first obtain a commercial
   license from OpenWorks LLP.  Please contact info@open-works.co.uk
   for information about commercial licensing.

   This software is provided by OpenWorks LLP "as is" and any express
   or implied warranties, including, but not limited to, the implied
   warranties of merchantability and fitness for a particular purpose
   are disclaimed.  In no event shall OpenWorks LLP be liable for any
   direct, indirect, incidental, special, exemplary, or consequential
   damages (including, but not limited to, procurement of substitute
   goods or services; loss of use, data, or profits; or business
   interruption) however caused and on any theory of liability,
   whether in contract, strict liability, or tort (including
   negligence or otherwise) arising in any way out of the use of this
   software, even if advised of the possibility of such damage.
*/

#include "libvex_basictypes.h"
#include "libvex_emwarn.h"
#include "libvex_guest_arm.h"
#include "libvex_ir.h"
#include "libvex.h"

#include "main_util.h"
#include "guest_generic_bb_to_IR.h"
#include "guest_arm_defs.h"


/* This file contains helper functions for arm guest code.  Calls to
   these functions are generated by the back end.  These calls are of
   course in the host machine code and this file will be compiled to
   host machine code, so that all makes sense.

   Only change the signatures of these helper functions very
   carefully.  If you change the signature here, you'll have to change
   the parameters passed to it in the IR calls constructed by
   guest-arm/toIR.c.
*/



/* generalised left-shifter */
static inline UInt lshift ( UInt x, Int n )
{
   if (n >= 0)
      return x << n;
   else
      return x >> (-n);
}


/* CALLED FROM GENERATED CODE: CLEAN HELPER */
/* Calculate NZCV from the supplied thunk components, in the positions
   they appear in the CPSR, viz bits 31:28 for N Z C V respectively.
   Returned bits 27:0 are zero. */
UInt armg_calculate_flags_nzcv ( UInt cc_op, UInt cc_dep1,
                                 UInt cc_dep2, UInt cc_dep3 )
{
   switch (cc_op) {
      case ARMG_CC_OP_COPY:
         /* (nzcv, unused, unused) */
         return cc_dep1;
      case ARMG_CC_OP_ADD: {
         /* (argL, argR, unused) */
         UInt argL = cc_dep1;
         UInt argR = cc_dep2;
         UInt res  = argL + argR;
         UInt nf   = lshift( res & (1<<31), ARMG_CC_SHIFT_N - 31 );
         UInt zf   = lshift( res == 0, ARMG_CC_SHIFT_Z );
         // CF and VF need verification
         UInt cf   = lshift( res < argL, ARMG_CC_SHIFT_C );
         UInt vf   = lshift( (res ^ argL) & (res ^ argR),
                             ARMG_CC_SHIFT_V + 1 - 32 )
                     & ARMG_CC_MASK_V;
         //vex_printf("%08x %08x -> n %x z %x c %x v %x\n",
         //           argL, argR, nf, zf, cf, vf);
         return nf | zf | cf | vf;
      }
      case ARMG_CC_OP_SUB: {
         /* (argL, argR, unused) */
         UInt argL = cc_dep1;
         UInt argR = cc_dep2;
         UInt res  = argL - argR;
         UInt nf   = lshift( res & (1<<31), ARMG_CC_SHIFT_N - 31 );
         UInt zf   = lshift( res == 0, ARMG_CC_SHIFT_Z );
         // XXX cf is inverted relative to normal sense
         UInt cf   = lshift( argL >= argR, ARMG_CC_SHIFT_C );
         UInt vf   = lshift( (argL ^ argR) & (argL ^ res),
                             ARMG_CC_SHIFT_V + 1 - 32 )
                     & ARMG_CC_MASK_V;
         //vex_printf("%08x %08x -> n %x z %x c %x v %x\n",
         //           argL, argR, nf, zf, cf, vf);
         return nf | zf | cf | vf;
      }
      case ARMG_CC_OP_ADC: {
         /* (argL, argR, oldC) */
         UInt argL = cc_dep1;
         UInt argR = cc_dep2;
         UInt oldC = cc_dep3;
         UInt res  = (argL + argR) + oldC;
         UInt nf   = lshift( res & (1<<31), ARMG_CC_SHIFT_N - 31 );
         UInt zf   = lshift( res == 0, ARMG_CC_SHIFT_Z );
         UInt cf   = oldC ? lshift( res <= argL, ARMG_CC_SHIFT_C )
                          : lshift( res <  argL, ARMG_CC_SHIFT_C );
         UInt vf   = lshift( (res ^ argL) & (res ^ argR),
                             ARMG_CC_SHIFT_V + 1 - 32 )
                     & ARMG_CC_MASK_V;
         //vex_printf("%08x %08x -> n %x z %x c %x v %x\n",
         //           argL, argR, nf, zf, cf, vf);
         return nf | zf | cf | vf;
      }
      case ARMG_CC_OP_SBB: {
         /* (argL, argR, oldC) */
         UInt argL = cc_dep1;
         UInt argR = cc_dep2;
         UInt oldC = cc_dep3;
         UInt res  = argL - argR - (oldC ^ 1);
         UInt nf   = lshift( res & (1<<31), ARMG_CC_SHIFT_N - 31 );
         UInt zf   = lshift( res == 0, ARMG_CC_SHIFT_Z );
         UInt cf   = oldC ? lshift( argL >= argR, ARMG_CC_SHIFT_C )
                          : lshift( argL >  argR, ARMG_CC_SHIFT_C );
         UInt vf   = lshift( (argL ^ argR) & (argL ^ res),
                             ARMG_CC_SHIFT_V + 1 - 32 )
                     & ARMG_CC_MASK_V;
         //vex_printf("%08x %08x -> n %x z %x c %x v %x\n",
         //           argL, argR, nf, zf, cf, vf);
         return nf | zf | cf | vf;
      }
      case ARMG_CC_OP_LOGIC: {
         /* (res, shco, oldV) */
         UInt res  = cc_dep1;
         UInt shco = cc_dep2;
         UInt oldV = cc_dep3;
         UInt nf   = lshift( res & (1<<31), ARMG_CC_SHIFT_N - 31 );
         UInt zf   = lshift( res == 0, ARMG_CC_SHIFT_Z );
         UInt cf   = lshift( shco & 1, ARMG_CC_SHIFT_C );
         UInt vf   = lshift( oldV & 1, ARMG_CC_SHIFT_V );
         return nf | zf | cf | vf;
      }
      case ARMG_CC_OP_MUL: {
         /* (res, unused, oldC:oldV) */
         UInt res  = cc_dep1;
         UInt oldC = (cc_dep3 >> 1) & 1;
         UInt oldV = (cc_dep3 >> 0) & 1;
         UInt nf   = lshift( res & (1<<31), ARMG_CC_SHIFT_N - 31 );
         UInt zf   = lshift( res == 0, ARMG_CC_SHIFT_Z );
         UInt cf   = lshift( oldC & 1, ARMG_CC_SHIFT_C );
         UInt vf   = lshift( oldV & 1, ARMG_CC_SHIFT_V );
         return nf | zf | cf | vf;
      }
      case ARMG_CC_OP_MULL: {
         /* (resLo32, resHi32, oldC:oldV) */
         UInt resLo32 = cc_dep1;
         UInt resHi32 = cc_dep2;
         UInt oldC    = (cc_dep3 >> 1) & 1;
         UInt oldV    = (cc_dep3 >> 0) & 1;
         UInt nf      = lshift( resHi32 & (1<<31), ARMG_CC_SHIFT_N - 31 );
         UInt zf      = lshift( (resHi32|resLo32) == 0, ARMG_CC_SHIFT_Z );
         UInt cf      = lshift( oldC & 1, ARMG_CC_SHIFT_C );
         UInt vf      = lshift( oldV & 1, ARMG_CC_SHIFT_V );
         return nf | zf | cf | vf;
      }
      default:
         /* shouldn't really make these calls from generated code */
         vex_printf("armg_calculate_flags_nzcv"
                    "( op=%u, dep1=0x%x, dep2=0x%x, dep3=0x%x )\n",
                    cc_op, cc_dep1, cc_dep2, cc_dep3 );
         vpanic("armg_calculate_flags_nzcv");
   }
}


/* CALLED FROM GENERATED CODE: CLEAN HELPER */
/* Calculate the C flag from the thunk components, in the lowest bit
   of the word (bit 0). */
UInt armg_calculate_flag_c ( UInt cc_op, UInt cc_dep1,
                             UInt cc_dep2, UInt cc_dep3 )
{
   UInt r = armg_calculate_flags_nzcv(cc_op, cc_dep1, cc_dep2, cc_dep3);
   return (r >> ARMG_CC_SHIFT_C) & 1;
}


/* CALLED FROM GENERATED CODE: CLEAN HELPER */
/* Calculate the V flag from the thunk components, in the lowest bit
   of the word (bit 0). */
UInt armg_calculate_flag_v ( UInt cc_op, UInt cc_dep1,
                             UInt cc_dep2, UInt cc_dep3 )
{
   UInt r = armg_calculate_flags_nzcv(cc_op, cc_dep1, cc_dep2, cc_dep3);
   return (r >> ARMG_CC_SHIFT_V) & 1;
}


/* CALLED FROM GENERATED CODE: CLEAN HELPER */
/* Calculate the specified condition from the thunk components, in the
   lowest bit of the word (bit 0). */
extern 
UInt armg_calculate_condition ( UInt cond_n_op /* ARMCondcode << 4 | cc_op */,
                                UInt cc_dep1,
                                UInt cc_dep2, UInt cc_dep3 )
{
   UInt cond  = cond_n_op >> 4;
   UInt cc_op = cond_n_op & 0xF;
   UInt nf, zf, vf, cf;
   UInt inv  = cond & 1;
   //   vex_printf("XXXXXXXX %x %x %x %x\n", cond_n_op, cc_dep1, cc_dep2, cc_dep3);
   UInt nzcv = armg_calculate_flags_nzcv(cc_op, cc_dep1, cc_dep2, cc_dep3);

   switch (cond) {
      case ARMCondEQ:    // Z=1         => z
      case ARMCondNE:    // Z=0
         zf = nzcv >> ARMG_CC_SHIFT_Z;
         return 1 & (inv ^ zf);

      case ARMCondHS:    // C=1         => c
      case ARMCondLO:    // C=0
         cf = nzcv >> ARMG_CC_SHIFT_C;
         return 1 & (inv ^ cf);

      case ARMCondMI:    // N=1         => n
      case ARMCondPL:    // N=0
         nf = nzcv >> ARMG_CC_SHIFT_N;
         return 1 & (inv ^ nf);

      case ARMCondVS:    // V=1         => v
      case ARMCondVC:    // V=0
         vf = nzcv >> ARMG_CC_SHIFT_V;
         return 1 & (inv ^ vf);

      case ARMCondHI:    // C=1 && Z=0   => c & ~z
      case ARMCondLS:    // C=0 || Z=1
         cf = nzcv >> ARMG_CC_SHIFT_C;
         zf = nzcv >> ARMG_CC_SHIFT_Z;
         return 1 & (inv ^ (cf & ~zf));

      case ARMCondGE:    // N=V          => ~(n^v)
      case ARMCondLT:    // N!=V
         nf = nzcv >> ARMG_CC_SHIFT_N;
         vf = nzcv >> ARMG_CC_SHIFT_V;
         return 1 & (inv ^ ~(nf ^ vf));

      case ARMCondGT:    // Z=0 && N=V   => ~z & ~(n^v)  =>  ~(z | (n^v))
      case ARMCondLE:    // Z=1 || N!=V
         nf = nzcv >> ARMG_CC_SHIFT_N;
         vf = nzcv >> ARMG_CC_SHIFT_V;
         zf = nzcv >> ARMG_CC_SHIFT_Z;
         return 1 & (inv ^ ~(zf | (nf ^ vf)));

      case ARMCondAL: // should never get here: Always => no flags to calc
      case ARMCondNV: // should never get here: Illegal instr
      default:
         /* shouldn't really make these calls from generated code */
         vex_printf("armg_calculate_condition(ARM)"
                    "( %u, %u, 0x%x, 0x%x, 0x%x )\n",
                    cond, cc_op, cc_dep1, cc_dep2, cc_dep3 );
         vpanic("armg_calculate_condition(ARM)");
   }
}


/*---------------------------------------------------------------*/
/*--- Flag-helpers translation-time function specialisers.    ---*/
/*--- These help iropt specialise calls the above run-time    ---*/
/*--- flags functions.                                        ---*/
/*---------------------------------------------------------------*/

/* Used by the optimiser to try specialisations.  Returns an
   equivalent expression, or NULL if none. */

static Bool isU32 ( IRExpr* e, UInt n )
{
   return
      toBool( e->tag == Iex_Const
              && e->Iex.Const.con->tag == Ico_U32
              && e->Iex.Const.con->Ico.U32 == n );
}

IRExpr* guest_arm_spechelper ( HChar* function_name,
                               IRExpr** args )
{
#  define unop(_op,_a1) IRExpr_Unop((_op),(_a1))
#  define binop(_op,_a1,_a2) IRExpr_Binop((_op),(_a1),(_a2))
#  define mkU32(_n) IRExpr_Const(IRConst_U32(_n))
#  define mkU8(_n)  IRExpr_Const(IRConst_U8(_n))

   Int i, arity = 0;
   for (i = 0; args[i]; i++)
      arity++;
#  if 0
   vex_printf("spec request:\n");
   vex_printf("   %s  ", function_name);
   for (i = 0; i < arity; i++) {
      vex_printf("  ");
      ppIRExpr(args[i]);
   }
   vex_printf("\n");
#  endif

   /* --------- specialising "x86g_calculate_condition" --------- */

   if (vex_streq(function_name, "armg_calculate_condition")) {
      /* specialise calls to above "armg_calculate condition" function */
      IRExpr *cond_n_op, *cc_dep1, *cc_dep2, *cc_dep3;
      vassert(arity == 4);
      cond_n_op = args[0]; /* ARMCondcode << 4  |  ARMG_CC_OP_* */
      cc_dep1   = args[1];
      cc_dep2   = args[2];
      cc_dep3   = args[3];

      /*---------------- SUB ----------------*/

      if (isU32(cond_n_op, (ARMCondEQ << 4) | ARMG_CC_OP_SUB)) {
         /* EQ after SUB --> test argL == argR */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpEQ32, cc_dep1, cc_dep2));
      }
      if (isU32(cond_n_op, (ARMCondNE << 4) | ARMG_CC_OP_SUB)) {
         /* NE after SUB --> test argL != argR */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpNE32, cc_dep1, cc_dep2));
      }

      if (isU32(cond_n_op, (ARMCondLE << 4) | ARMG_CC_OP_SUB)) {
         /* LE after SUB --> test argL <=s argR */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpLE32S, cc_dep1, cc_dep2));
      }

      if (isU32(cond_n_op, (ARMCondLT << 4) | ARMG_CC_OP_SUB)) {
         /* LE after SUB --> test argL <s argR */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpLT32S, cc_dep1, cc_dep2));
      }

      if (isU32(cond_n_op, (ARMCondGE << 4) | ARMG_CC_OP_SUB)) {
         /* GE after SUB --> test argL >=s argR
                         --> test argR <=s argL */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpLE32S, cc_dep2, cc_dep1));
      }

      if (isU32(cond_n_op, (ARMCondHS << 4) | ARMG_CC_OP_SUB)) {
         /* HS after SUB --> test argL >=u argR
                         --> test argR <=u argL */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpLE32U, cc_dep2, cc_dep1));
      }

      if (isU32(cond_n_op, (ARMCondLS << 4) | ARMG_CC_OP_SUB)) {
         /* LS after SUB --> test argL <=u argR */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpLE32U, cc_dep1, cc_dep2));
      }

      /*---------------- LOGIC ----------------*/
      if (isU32(cond_n_op, (ARMCondEQ << 4) | ARMG_CC_OP_LOGIC)) {
         /* EQ after LOGIC --> test res == 0 */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpEQ32, cc_dep1, mkU32(0)));
      }
      if (isU32(cond_n_op, (ARMCondNE << 4) | ARMG_CC_OP_LOGIC)) {
         /* NE after LOGIC --> test res != 0 */
         return unop(Iop_1Uto32,
                     binop(Iop_CmpNE32, cc_dep1, mkU32(0)));
      }

   }

#  undef unop
#  undef binop
#  undef mkU32
#  undef mkU8

   return NULL;
}


/*----------------------------------------------*/
/*--- The exported fns ..                    ---*/
/*----------------------------------------------*/

/* VISIBLE TO LIBVEX CLIENT */
#if 0
void LibVEX_GuestARM_put_flags ( UInt flags_native,
                                 /*OUT*/VexGuestARMState* vex_state )
{
   vassert(0); // FIXME

   /* Mask out everything except N Z V C. */
   flags_native
      &= (ARMG_CC_MASK_N | ARMG_CC_MASK_Z | ARMG_CC_MASK_V | ARMG_CC_MASK_C);
   
   vex_state->guest_CC_OP   = ARMG_CC_OP_COPY;
   vex_state->guest_CC_DEP1 = flags_native;
   vex_state->guest_CC_DEP2 = 0;
   vex_state->guest_CC_DEP3 = 0;
}
#endif

/* VISIBLE TO LIBVEX CLIENT */
UInt LibVEX_GuestARM_get_cpsr ( /*IN*/VexGuestARMState* vex_state )
{
   UInt nzcv;
   nzcv = armg_calculate_flags_nzcv(
              vex_state->guest_CC_OP,
              vex_state->guest_CC_DEP1,
              vex_state->guest_CC_DEP2,
              vex_state->guest_CC_DEP3
           );
   return nzcv;
}

/* VISIBLE TO LIBVEX CLIENT */
void LibVEX_GuestARM_initialise ( /*OUT*/VexGuestARMState* vex_state )
{
   vex_state->guest_R0  = 0;
   vex_state->guest_R1  = 0;
   vex_state->guest_R2  = 0;
   vex_state->guest_R3  = 0;
   vex_state->guest_R4  = 0;
   vex_state->guest_R5  = 0;
   vex_state->guest_R6  = 0;
   vex_state->guest_R7  = 0;
   vex_state->guest_R8  = 0;
   vex_state->guest_R9  = 0;
   vex_state->guest_R10 = 0;
   vex_state->guest_R11 = 0;
   vex_state->guest_R12 = 0;
   vex_state->guest_R13 = 0;
   vex_state->guest_R14 = 0;
   vex_state->guest_R15 = 0;

   vex_state->guest_CC_OP   = ARMG_CC_OP_COPY;
   vex_state->guest_CC_DEP1 = 0;
   vex_state->guest_CC_DEP2 = 0;
   vex_state->guest_CC_DEP3 = 0;

   vex_state->guest_EMWARN  = 0;
   vex_state->guest_TISTART = 0;
   vex_state->guest_TILEN   = 0;
   vex_state->guest_NRADDR  = 0;
   vex_state->guest_IP_AT_SYSCALL = 0;

   vex_state->guest_D0  = 0;
   vex_state->guest_D1  = 0;
   vex_state->guest_D2  = 0;
   vex_state->guest_D3  = 0;
   vex_state->guest_D4  = 0;
   vex_state->guest_D5  = 0;
   vex_state->guest_D6  = 0;
   vex_state->guest_D7  = 0;
   vex_state->guest_D8  = 0;
   vex_state->guest_D9  = 0;
   vex_state->guest_D10 = 0;
   vex_state->guest_D11 = 0;
   vex_state->guest_D12 = 0;
   vex_state->guest_D13 = 0;
   vex_state->guest_D14 = 0;
   vex_state->guest_D15 = 0;

   /* ARM encoded; zero is the default as it happens (result flags
      (NZCV) cleared, FZ disabled, round to nearest, non-vector mode,
      all exns masked, all exn sticky bits cleared). */
   vex_state->guest_FPSCR = 0;

   /* vex_state->padding1 = 0; */
   /* vex_state->padding2 = 0; */
}


/*-----------------------------------------------------------*/
/*--- Describing the arm guest state, for the benefit     ---*/
/*--- of iropt and instrumenters.                         ---*/
/*-----------------------------------------------------------*/

/* Figure out if any part of the guest state contained in minoff
   .. maxoff requires precise memory exceptions.  If in doubt return
   True (but this is generates significantly slower code).  

   We enforce precise exns for guest R13(sp), R15(pc).
*/
Bool guest_arm_state_requires_precise_mem_exns ( Int minoff, 
                                                 Int maxoff)
{
   Int sp_min = offsetof(VexGuestARMState, guest_R13);
   Int sp_max = sp_min + 4 - 1;
   Int pc_min = offsetof(VexGuestARMState, guest_R15);
   Int pc_max = pc_min + 4 - 1;

   if (maxoff < sp_min || minoff > sp_max) {
      /* no overlap with sp */
   } else {
      return True;
   }

   if (maxoff < pc_min || minoff > pc_max) {
      /* no overlap with pc */
   } else {
      return True;
   }

   /* We appear to need precise updates of R11 in order to get proper
      stacktraces from non-optimised code. */
   Int r11_min = offsetof(VexGuestARMState, guest_R11);
   Int r11_max = r11_min + 4 - 1;

   if (maxoff < r11_min || minoff > r11_max) {
      /* no overlap with pc */
   } else {
      return True;
   }

   return False;
}



#define ALWAYSDEFD(field)                           \
    { offsetof(VexGuestARMState, field),            \
      (sizeof ((VexGuestARMState*)0)->field) }

VexGuestLayout
   armGuest_layout 
      = { 
          /* Total size of the guest state, in bytes. */
          .total_sizeB = sizeof(VexGuestARMState),

          /* Describe the stack pointer. */
          .offset_SP = offsetof(VexGuestARMState,guest_R13),
          .sizeof_SP = 4,

          /* Describe the instruction pointer. */
          .offset_IP = offsetof(VexGuestARMState,guest_R15),
          .sizeof_IP = 4,

          /* Describe any sections to be regarded by Memcheck as
             'always-defined'. */
          .n_alwaysDefd = 7,

          /* flags thunk: OP is always defd, whereas DEP1 and DEP2
             have to be tracked.  See detailed comment in gdefs.h on
             meaning of thunk fields. */
          .alwaysDefd
             = { /* */ ALWAYSDEFD(guest_R15),
                 /* */ ALWAYSDEFD(guest_CC_OP),
                 /* */ ALWAYSDEFD(guest_EMWARN),
                 /* */ ALWAYSDEFD(guest_TISTART),
                 /* */ ALWAYSDEFD(guest_TILEN),
                 /* */ ALWAYSDEFD(guest_NRADDR),
                 /* */ ALWAYSDEFD(guest_IP_AT_SYSCALL)
               }
        };


/*---------------------------------------------------------------*/
/*--- end                                 guest_arm_helpers.c ---*/
/*---------------------------------------------------------------*/
