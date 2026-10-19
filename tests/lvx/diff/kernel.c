/*
 * A kernel of real size, for the timing model.
 *
 * Every other test here is a micro test or runs the atomic CPU, and that is
 * how three timing bugs reached the tree in one week with twenty-odd timing
 * assertions passing over them:
 *
 *   - the decoder never told fetch how long a bundle was, so every change of
 *     bundle length read as a taken branch and squashed the pipeline.  Worth
 *     six times the cycles on code whose bundle lengths vary, which is all
 *     compiled code and none of the hand-written fixtures.
 *   - a taken-branch bubble was not a reason to tick, so MinorCPU deadlocked
 *     outright when a bubble, a squashed front end and a drained pipeline
 *     coincided.  Found by somebody else's PolyBench run after 1h19m.
 *   - FDIV/FSQRT had no functional-unit bucket of their own and ran two
 *     cycles short.
 *
 * None of those is subtle once a kernel of this shape runs under the pipeline
 * model.  So the shape is deliberate, each part aimed at one of them:
 *
 *   a dependence-carrying stencil   each element reads its west and north
 *                                   neighbours, so the recurrence is real and
 *                                   the schedule cannot hide it.  This is the
 *                                   shape where minor diverges furthest from
 *                                   atomic (2.5x on PolyBench's seidel-2d).
 *   sqrt and divide on the hot path the FD-stage opcodes, and the FP context
 *                                   the deadlock was found in.
 *   -O2 compiled C                  bundle lengths vary, immediates reach
 *                                   IMMX, and GCC emits hardware loops.
 *
 * It self-checks: the stencil is seeded deterministically and reduced to an
 * integer, so a model that completes with the wrong answer fails too, not
 * just one that hangs.  Returns 0 on success, 1 on a wrong result.
 */

#define N 24
#define ITERS 12

static float a[N][N];

#ifdef KERNEL_HOST
#include <math.h>
#include <stdio.h>
static inline float lvx_sqrtf(float x) { return sqrtf(x); }
#else
/* The FD-stage opcode, written out rather than left to the compiler: this
   file is built -fno-builtin (as the whole harness is), under which
   __builtin_sqrtf becomes a call to sqrtf that a freestanding link cannot
   resolve.  Spelling it makes the instruction under test unconditional
   instead of a property of the flags. */
static inline float
lvx_sqrtf(float x)
{
	float r;
	__asm__ ("fsqrtw %0 = %1\n\t;;" : "=r"(r) : "r"(x));
	return r;
}
#endif

static unsigned long
kernel_run(void)
{
	/* Deterministic seed, integer arithmetic so it cannot itself depend on
	   the FP model being right. */
	for (int i = 0; i < N; i++)
		for (int j = 0; j < N; j++)
			a[i][j] = (float)((i * 7 + j * 3) & 15) + 1.0f;

	/* The recurrence: west and north neighbours, so trip i+1 waits on trip
	   i.  The divide and the sqrt sit inside it on purpose -- a long
	   latency inside a loop-carried dependence is what the FD bucket and
	   the load shadow both bear on. */
	/* Opaque, so the division cannot be strength-reduced away: fdivw is
	   the other FD-stage opcode and belongs in the recurrence too. */
	volatile float three = 3.0f;
	float d = three;

	for (int t = 0; t < ITERS; t++) {
		for (int i = 1; i < N; i++) {
			for (int j = 1; j < N; j++) {
				float w = a[i][j - 1];
				float n = a[i - 1][j];
				float s = (w + n + a[i][j]) / d;
				a[i][j] = lvx_sqrtf(s) + 1.0f;
			}
		}
	}

	/* Reduce to an integer so the check is exact and the exit code can
	   carry it. */
	unsigned long sum = 0;
	for (int i = 0; i < N; i++)
		for (int j = 0; j < N; j++)
			sum += (unsigned long)(a[i][j] * 64.0f);

	return sum;
}

#ifdef KERNEL_HOST
/* The host build prints the answer; check_kernel.sh feeds it back as
   KERNEL_EXPECTED, so the expected value is derived rather than written down
   -- the same differential discipline as run_diff.sh, on a program whose
   point is the timing model rather than the semantics. */
int main(void) { printf("%lu\n", kernel_run()); return 0; }
#else
int main(void) { return kernel_run() == KERNEL_EXPECTED ? 0 : 1; }
#endif
