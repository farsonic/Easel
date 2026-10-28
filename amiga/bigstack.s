; bigstack.s - run a C function on a stack of our own (exec StackSwap), for decoders such as
; dav1d that want far more stack than a program is started with.
;
; long run_on_stack(struct StackSwapStruct *ss, long (*fn)(void *), void *arg)
; ss describes the new stack (stk_Lower, stk_Upper, stk_Pointer = stk_Upper); StackSwap swaps it
; in, fn(arg) runs, and the second StackSwap (ss now holds the old stack) puts the old one back.

	section	code,code
	xdef	_run_on_stack

_run_on_stack:
	movem.l	d2-d7/a2-a6,-(sp)	; 11 registers: 44 bytes
	move.l	48(sp),a3		; ss
	move.l	52(sp),a2		; fn
	move.l	56(sp),d3		; arg
	move.l	4.w,a6
	move.l	a3,a0
	jsr	-732(a6)		; StackSwap(ss)
	move.l	d3,-(sp)
	jsr	(a2)
	addq.l	#4,sp
	move.l	d0,d2
	move.l	4.w,a6
	move.l	a3,a0
	jsr	-732(a6)		; StackSwap(ss): back to the old stack
	move.l	d2,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts
