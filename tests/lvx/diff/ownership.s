	## LVX system-register ownership functional test (US11995218).
	##
	## The ISS gates every GET/SET/WFX of a system register on the protection
	## ring the code is running in (PS.PL, PL0 the most privileged) against the
	## ring that owns each of that register's bit-fields.  SE-mode programs run
	## at PL0 and so never see it, which is exactly why it needs a test that
	## leaves PL0 deliberately.
	##
	## $men (MEN, Miscellaneous External Notifications) is the field to use: its
	## one bit-field is owned by MO.MEN, which resets to PL0, and its rerror is
	## READ0 -- so a read from a less privileged ring is not a trap, it returns
	## zero for that field.  That gives an observable difference with no trap
	## and no architectural state to unwind.
	##
	## $r0 holds the number of the case in progress, so the process exit code is
	## 0 on full success or the index of the first failing case otherwise.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: at PL0, MEN reads back what was written.
	maked $r0 = 1
	;;
	maked $r1 = 0xffff
	;;
	set $men = $r1
	;;
	get $r2 = $men
	;;
	ccb.dne $r2, $r1 ? fail
	;;

	## case 2: at PL1, MEN.MEN is owned by PL0 and reads as zero.
	maked $r0 = 2
	;;
	maked $r3 = 1
	;;
	set $ps = $r3
	;;
	get $r4 = $men
	;;
	maked $r5 = 0
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## The register itself is untouched: only the read was masked.  Reading it
	## back would need PL0 again, and returning to PL0 is itself a PL0-owned
	## write (PS.PL is owned by PSO.PL0/PSO.PL1), so the test ends here.
	maked $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
